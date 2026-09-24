/**
 * Vita Music Assistant - Asynchronous Image Loader implementation
 * Uses LRU eviction to keep memory bounded instead of clearing entire cache.
 */

#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "app/webrtc_client.hpp"
#include <fstream>
#include <vector>
#include <chrono>
#include <algorithm>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

// stb_image: decode JPEG/PNG/BMP/GIF to RGBA on worker threads for the NVG cover
// path. STB_IMAGE_STATIC makes these symbols file-local so they don't clash with
// the copy bundled inside nanovg.c. The Music Assistant image proxy serves
// stb-decodable formats (the same decoder brls::Image already relied on).
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace vita_ma {

// Fetch image bytes either directly over HTTP(S) or - when the URL is a
// server-relative path like "/imageproxy?..." (remote-access mode) - through
// the WebRTC http-proxy tunnel. Called from background threads only.
static HttpResponse fetchImageBytes(const std::string& url) {
    if (!url.empty() && url[0] == '/') {
        HttpResponse resp;
        int status = 0;
        std::string body;
        if (WebRTCClient::instance().httpProxyFetch("GET", url, 20000, status, body) &&
            status >= 200 && status < 300) {
            resp.success = true;
            resp.statusCode = status;
            resp.body = std::move(body);
        }
        return resp;
    }
    HttpClient client;
    return client.get(url);
}

std::map<std::string, ImageLoader::CacheEntry> ImageLoader::s_cache;
std::list<std::string> ImageLoader::s_lruOrder;
std::mutex ImageLoader::s_cacheMutex;
std::atomic<uint64_t> ImageLoader::s_generation{0};
std::atomic<bool> ImageLoader::s_paused{false};

std::queue<ImageLoader::PendingTextureUpdate> ImageLoader::s_pendingTextures;
std::mutex ImageLoader::s_pendingMutex;
std::atomic<bool> ImageLoader::s_pendingScheduled{false};
std::atomic<bool> ImageLoader::s_deferTextureUploads{false};

std::queue<ImageLoader::PendingCoverUpload> ImageLoader::s_pendingCovers;
std::mutex ImageLoader::s_pendingCoverMutex;
std::atomic<bool> ImageLoader::s_pendingCoverScheduled{false};

void ImageLoader::setPaused(bool paused) {
    s_paused.store(paused);
    if (paused) {
        brls::Logger::info("ImageLoader: Paused - new thumbnail loads disabled");
    } else {
        brls::Logger::info("ImageLoader: Resumed - thumbnail loads re-enabled");
    }
}

bool ImageLoader::isPaused() {
    return s_paused.load();
}

size_t ImageLoader::getCacheSize() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    return s_cache.size();
}

void ImageLoader::loadAsync(const std::string& url, LoadCallback callback,
                            brls::Image* target, std::shared_ptr<std::atomic<bool>> alive) {
    if (url.empty() || !target || !alive) return;

    // Skip new loads while paused (playback in progress)
    if (s_paused.load()) return;

    // Capture the current generation so stale callbacks are skipped after cancelAll()
    uint64_t gen = s_generation.load();

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(url);
        if (it != s_cache.end()) {
            // Promote to front of LRU list (most recently used)
            s_lruOrder.erase(it->second.lruIt);
            s_lruOrder.push_front(url);
            it->second.lruIt = s_lruOrder.begin();

            // Even on a cache hit the GPU upload (setImageFromMem) costs ~15-20ms,
            // so queue it for the batched/deferred uploader instead of stalling
            // the current frame.
            std::vector<uint8_t> dataCopy = it->second.data;
            queueTextureUpdate(std::move(dataCopy), target, callback, alive, gen);
            return;
        }
    }

    // Load asynchronously
    brls::async([url, callback, target, alive, gen]() {
        // Check if cancelled before making the HTTP request.
        if (!alive->load() || gen != s_generation.load()) return;

        HttpResponse resp = fetchImageBytes(url);

        if (resp.success && !resp.body.empty()) {
            // Cache the image data
            std::vector<uint8_t> imageData(resp.body.begin(), resp.body.end());

            {
                std::lock_guard<std::mutex> lock(s_cacheMutex);

                // LRU eviction: remove oldest entries until we're under the limit
                while (s_cache.size() >= MAX_CACHE_SIZE && !s_lruOrder.empty()) {
                    const std::string& oldest = s_lruOrder.back();
                    s_cache.erase(oldest);
                    s_lruOrder.pop_back();
                }

                // Insert new entry at front of LRU
                s_lruOrder.push_front(url);
                CacheEntry entry;
                entry.data = imageData;
                entry.lruIt = s_lruOrder.begin();
                s_cache[url] = std::move(entry);
            }

            // Hand the decoded bytes to the batched uploader. It performs the
            // GPU upload on the main thread (a few per frame), and skips the
            // upload if the target was destroyed or cancelAll() bumped the
            // generation in the meantime.
            queueTextureUpdate(std::move(imageData), target, callback, alive, gen);
        }
    });
}

void ImageLoader::setDeferTextureUploads(bool defer) {
    bool wasDeferred = s_deferTextureUploads.exchange(defer);
    // Transitioning from deferred -> allowed: wake the processor so the queued
    // uploads start draining immediately instead of waiting for the next event.
    if (wasDeferred && !defer) {
        bool expected = false;
        if (s_pendingScheduled.compare_exchange_strong(expected, true)) {
            brls::sync([]() { processPendingTextures(); });
        }
    }
}

void ImageLoader::queueTextureUpdate(std::vector<uint8_t> data, brls::Image* target,
                                     LoadCallback callback,
                                     std::shared_ptr<std::atomic<bool>> alive, uint64_t gen) {
    {
        std::lock_guard<std::mutex> lock(s_pendingMutex);
        s_pendingTextures.push({std::move(data), target, std::move(callback), std::move(alive), gen});
    }
    // Schedule a drain on the main thread if one isn't already pending.
    bool expected = false;
    if (s_pendingScheduled.compare_exchange_strong(expected, true)) {
        brls::sync([]() { processPendingTextures(); });
    }
}

void ImageLoader::processPendingTextures() {
    s_pendingScheduled.store(false);

    // While scrolling, hold everything and re-check next frame so no upload
    // stalls the scroll frame.
    if (s_deferTextureUploads.load()) {
        bool morePending;
        {
            std::lock_guard<std::mutex> lock(s_pendingMutex);
            morePending = !s_pendingTextures.empty();
        }
        if (morePending) {
            bool expected = false;
            if (s_pendingScheduled.compare_exchange_strong(expected, true)) {
                brls::sync([]() { processPendingTextures(); });
            }
        }
        return;
    }

    // Upload at most MAX_TEXTURES_PER_FRAME this frame, bounded by an 8ms budget
    // so we don't blow the 16.7ms frame even if an upload runs long.
    auto frameStart = std::chrono::steady_clock::now();
    static constexpr int64_t MAX_UPLOAD_TIME_US = 8000;

    int processed = 0;
    while (processed < MAX_TEXTURES_PER_FRAME) {
        if (processed > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - frameStart).count();
            if (elapsed >= MAX_UPLOAD_TIME_US) break;
        }

        PendingTextureUpdate update;
        {
            std::lock_guard<std::mutex> lock(s_pendingMutex);
            if (s_pendingTextures.empty()) return;
            update = std::move(s_pendingTextures.front());
            s_pendingTextures.pop();
        }

        // Drop the upload if the target view was destroyed, cancelAll() bumped
        // the generation, or there's nothing to upload.
        if (!update.target) continue;
        if (update.alive && !update.alive->load()) continue;
        if (update.gen != s_generation.load()) continue;
        if (update.data.empty()) continue;

        update.target->setImageFromMem(update.data.data(), update.data.size());
        if (update.callback) update.callback(update.target);
        processed++;
    }

    // Re-schedule if more remain.
    bool morePending;
    {
        std::lock_guard<std::mutex> lock(s_pendingMutex);
        morePending = !s_pendingTextures.empty();
    }
    if (morePending) {
        bool expected = false;
        if (s_pendingScheduled.compare_exchange_strong(expected, true)) {
            brls::sync([]() { processPendingTextures(); });
        }
    }
}

bool ImageLoader::cacheGetBytes(const std::string& url, std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    auto it = s_cache.find(url);
    if (it == s_cache.end()) return false;
    // Promote to most-recently-used.
    s_lruOrder.erase(it->second.lruIt);
    s_lruOrder.push_front(url);
    it->second.lruIt = s_lruOrder.begin();
    out = it->second.data;
    return true;
}

void ImageLoader::cachePutBytes(const std::string& url, std::vector<uint8_t> data) {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    if (s_cache.find(url) != s_cache.end()) return;  // already cached
    while (s_cache.size() >= MAX_CACHE_SIZE && !s_lruOrder.empty()) {
        const std::string& oldest = s_lruOrder.back();
        s_cache.erase(oldest);
        s_lruOrder.pop_back();
    }
    s_lruOrder.push_front(url);
    CacheEntry entry;
    entry.data = std::move(data);
    entry.lruIt = s_lruOrder.begin();
    s_cache[url] = std::move(entry);
}

void ImageLoader::loadCoverAsync(const std::string& url, CoverReadyCallback callback,
                                 std::shared_ptr<std::atomic<bool>> alive) {
    if (url.empty() || !alive || !callback) return;
    if (s_paused.load()) return;

    uint64_t gen = s_generation.load();

    brls::async([url, callback, alive, gen]() {
        if (!alive->load() || gen != s_generation.load()) return;

        // Get the raw encoded bytes: cache first, otherwise download and cache.
        std::vector<uint8_t> bytes;
        if (!cacheGetBytes(url, bytes)) {
            HttpResponse resp = fetchImageBytes(url);
            if (!resp.success || resp.body.empty()) return;
            bytes.assign(resp.body.begin(), resp.body.end());
            cachePutBytes(url, bytes);
        }

        if (!alive->load() || gen != s_generation.load()) return;

        // Decode to RGBA here, off the main thread. The main thread then only
        // has to hand the pixels to the GPU (nvgCreateImageRGBA).
        int w = 0, h = 0, channels = 0;
        unsigned char* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                    &w, &h, &channels, 4);
        if (!rgba) return;
        std::vector<uint8_t> rgbaVec(rgba, rgba + static_cast<size_t>(w) * h * 4);
        stbi_image_free(rgba);

        queueCoverUpload(std::move(rgbaVec), w, h, callback, alive, gen);
    });
}

void ImageLoader::queueCoverUpload(std::vector<uint8_t> rgba, int w, int h,
                                   CoverReadyCallback callback,
                                   std::shared_ptr<std::atomic<bool>> alive, uint64_t gen) {
    {
        std::lock_guard<std::mutex> lock(s_pendingCoverMutex);
        s_pendingCovers.push({std::move(rgba), w, h, std::move(callback), std::move(alive), gen});
    }
    bool expected = false;
    if (s_pendingCoverScheduled.compare_exchange_strong(expected, true)) {
        brls::sync([]() { processPendingCovers(); });
    }
}

void ImageLoader::processPendingCovers() {
    s_pendingCoverScheduled.store(false);

    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg) return;

    int uploaded = 0;
    while (uploaded < MAX_COVERS_PER_FRAME) {
        PendingCoverUpload up;
        {
            std::lock_guard<std::mutex> lock(s_pendingCoverMutex);
            if (s_pendingCovers.empty()) return;
            up = std::move(s_pendingCovers.front());
            s_pendingCovers.pop();
        }

        // Skip if the cell was destroyed or cancelAll() bumped the generation.
        if (up.alive && !up.alive->load()) continue;
        if (up.gen != s_generation.load()) continue;
        if (up.rgba.empty() || up.width <= 0 || up.height <= 0) continue;

        int nvgImg = nvgCreateImageRGBA(vg, up.width, up.height, 0, up.rgba.data());
        if (nvgImg != 0 && up.callback) {
            up.callback(nvgImg, up.width, up.height);
        }
        uploaded++;
    }

    bool morePending;
    {
        std::lock_guard<std::mutex> lock(s_pendingCoverMutex);
        morePending = !s_pendingCovers.empty();
    }
    if (morePending) {
        bool expected = false;
        if (s_pendingCoverScheduled.compare_exchange_strong(expected, true)) {
            brls::sync([]() { processPendingCovers(); });
        }
    }
}

bool ImageLoader::loadFromFile(const std::string& path, brls::Image* target) {
    if (path.empty() || !target) return false;

#ifdef __vita__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;

    // Get file size
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    if (size <= 0 || size > 4 * 1024 * 1024) {  // Max 4MB for cover art
        sceIoClose(fd);
        return false;
    }

    std::vector<uint8_t> data(size);
    int read = sceIoRead(fd, data.data(), size);
    sceIoClose(fd);

    if (read != size) return false;
#else
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto size = file.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024) return false;

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();
#endif

    target->setImageFromMem(data.data(), data.size());
    return true;
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_cache.clear();
    s_lruOrder.clear();
}

void ImageLoader::cancelAll() {
    // Increment generation counter - all in-flight downloads will see a stale
    // generation and skip their texture uploads, preventing use-after-free
    s_generation.fetch_add(1);

    // Navigation/playback transitions call this; make sure a grid that was
    // scrolling when it went away didn't leave uploads globally deferred. This
    // also drains any now-stale queued uploads (they're skipped on generation
    // mismatch).
    setDeferTextureUploads(false);
}

} // namespace vita_ma
