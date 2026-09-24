/**
 * Vita Music Assistant - QR Scanner Activity implementation
 *
 * Opens the back camera (640x480 ABGR), shows a live preview through a custom
 * NVG-textured view, and runs the quirc decoder over grayscale frames until a
 * QR code is found. The decoded payload is handed to the caller and the
 * activity closes itself.
 */

#include "activity/qr_scanner_activity.hpp"

#include <algorithm>
#include <cstring>

extern "C" {
#include "quirc.h"
}

#ifdef __vita__
#include <psp2/camera.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#endif

namespace vita_ma {

// ---------------------------------------------------------------------------
// Live preview: uploads the latest camera frame into an NVG texture and draws
// it letterboxed inside its bounds, with a cyan scan frame on top.
class CameraPreviewView : public brls::View {
public:
    explicit CameraPreviewView(QrScannerActivity* owner) : m_owner(owner) {}

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        QrScannerActivity* o = m_owner;
        if (!o) return;

        // Upload the newest frame (RGBA) into the texture
        if (o->m_frameDirty.load()) {
            std::lock_guard<std::mutex> lock(o->m_frameMutex);
            if (!o->m_frameRgba.empty()) {
                if (o->m_previewTex == 0) {
                    o->m_previewTex = nvgCreateImageRGBA(
                        vg, QrScannerActivity::CAM_W, QrScannerActivity::CAM_H,
                        0, o->m_frameRgba.data());
                } else {
                    nvgUpdateImage(vg, o->m_previewTex, o->m_frameRgba.data());
                }
            }
            o->m_frameDirty.store(false);
        }

        if (o->m_previewTex != 0) {
            NVGpaint paint = nvgImagePattern(vg, x, y, width, height, 0.0f,
                                             o->m_previewTex, 1.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, 12.0f);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }

        // Cyan scan frame: four corner brackets around the center square
        const float frame = std::min(width, height) * 0.62f;
        const float fx = x + (width - frame) / 2.0f;
        const float fy = y + (height - frame) / 2.0f;
        const float arm = frame * 0.18f;
        nvgStrokeColor(vg, nvgRGB(0x00, 0xbc, 0xee));
        nvgStrokeWidth(vg, 3.0f);
        auto corner = [&](float cx, float cy, float dx, float dy) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx + dx * arm, cy);
            nvgLineTo(vg, cx, cy);
            nvgLineTo(vg, cx, cy + dy * arm);
            nvgStroke(vg);
        };
        corner(fx, fy, 1.0f, 1.0f);                    // top-left
        corner(fx + frame, fy, -1.0f, 1.0f);           // top-right
        corner(fx, fy + frame, 1.0f, -1.0f);           // bottom-left
        corner(fx + frame, fy + frame, -1.0f, -1.0f);  // bottom-right
    }

    void detach() { m_owner = nullptr; }

private:
    QrScannerActivity* m_owner;
};

// ---------------------------------------------------------------------------

QrScannerActivity::QrScannerActivity(std::function<void(const std::string&)> onResult)
    : m_onResult(std::move(onResult)) {}

QrScannerActivity::~QrScannerActivity() {
    stopCamera();
    if (m_qr) {
        quirc_destroy(m_qr);
        m_qr = nullptr;
    }
}

brls::View* QrScannerActivity::createContentView() {
    return brls::View::createFromXMLResource("music/activity/qr_scanner.xml");
}

void QrScannerActivity::onContentAvailable() {
    // Back closes the scanner without a result
    this->getContentView()->registerAction(
        "Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::Sound::SOUND_BACK);

    if (previewHolder) {
        auto* preview = new CameraPreviewView(this);
        preview->setGrow(1.0f);
        preview->setWidthPercentage(100.0f);
        previewHolder->addView(preview);
    }

    if (!startCamera()) {
        if (statusLabel) statusLabel->setText("Camera not available");
    }
}

void QrScannerActivity::willDisappear(bool resetState) {
    stopCamera();
    brls::Activity::willDisappear(resetState);
}

bool QrScannerActivity::startCamera() {
#ifdef __vita__
    if (m_running.load()) return true;

    // Max out CPU/GPU clocks: quirc's finder-pattern search is heavy, and at
    // stock clocks it can't keep up with the preview (choppy) and only ever
    // decodes stale frames. At 444MHz it decodes several full frames/sec.
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // Camera output buffer must live in CDRAM
    const SceSize bufSize = (CAM_W * CAM_H * 4 + 0x3ffff) & ~0x3ffff;  // 256KB aligned
    m_memblock = sceKernelAllocMemBlock("qr_camera",
                                        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                        bufSize, nullptr);
    if (m_memblock < 0) {
        brls::Logger::error("QR: camera buffer alloc failed: 0x{:08x}", (unsigned)m_memblock);
        return false;
    }
    sceKernelGetMemBlockBase(m_memblock, &m_camBase);

    SceCameraInfo info = {};
    info.size = sizeof(SceCameraInfo);
    info.format = SCE_CAMERA_FORMAT_ABGR;   // bytes R,G,B,A - matches NVG RGBA
    info.resolution = SCE_CAMERA_RESOLUTION_640_480;
    info.framerate = SCE_CAMERA_FRAMERATE_15_FPS;
    info.sizeIBase = CAM_W * CAM_H * 4;
    info.pIBase = m_camBase;
    info.pitch = 0;

    int dev = SCE_CAMERA_DEVICE_BACK;
    int ret = sceCameraOpen(dev, &info);
    if (ret < 0) {
        // Fall back to the front camera (e.g. back camera busy/absent)
        dev = SCE_CAMERA_DEVICE_FRONT;
        ret = sceCameraOpen(dev, &info);
    }
    if (ret < 0) {
        brls::Logger::error("QR: sceCameraOpen failed: 0x{:08x}", (unsigned)ret);
        sceKernelFreeMemBlock(m_memblock);
        m_memblock = -1;
        return false;
    }
    m_cameraOpen = true;

    ret = sceCameraStart(dev);
    if (ret < 0) {
        brls::Logger::error("QR: sceCameraStart failed: 0x{:08x}", (unsigned)ret);
        sceCameraClose(dev);
        m_cameraOpen = false;
        sceKernelFreeMemBlock(m_memblock);
        m_memblock = -1;
        return false;
    }

    m_qr = quirc_new();
    if (m_qr && quirc_resize(m_qr, CAM_W, CAM_H) < 0) {
        quirc_destroy(m_qr);
        m_qr = nullptr;
    }
    if (!m_qr) {
        brls::Logger::error("QR: quirc allocation failed");
        sceCameraStop(dev);
        sceCameraClose(dev);
        m_cameraOpen = false;
        sceKernelFreeMemBlock(m_memblock);
        m_memblock = -1;
        return false;
    }

    m_camDevice = dev;
    m_frameRgba.assign(CAM_W * CAM_H * 4, 0);
    m_decodeRgba.assign(CAM_W * CAM_H * 4, 0);
    m_running.store(true);
    m_camThread = std::thread([this]() { cameraLoop(); });
    m_decodeThread = std::thread([this]() { decodeLoop(); });
    return true;
#else
    (void)m_previewTex;
    return false;
#endif
}

// Camera thread: only reads frames and refreshes the preview buffer. Kept
// deliberately cheap (a single memcpy) so the preview stays smooth no matter
// how long a decode takes on the other thread.
void QrScannerActivity::cameraLoop() {
#ifdef __vita__
    while (m_running.load()) {
        SceCameraRead read = {};
        read.size = sizeof(SceCameraRead);
        read.mode = 0;  // blocking read
        int ret = sceCameraRead(m_camDevice, &read);
        if (ret < 0) {
            brls::Logger::error("QR: sceCameraRead failed: 0x{:08x}", (unsigned)ret);
            break;
        }
        if (!m_running.load()) break;

        const unsigned char* frame = static_cast<const unsigned char*>(m_camBase);
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            std::memcpy(m_frameRgba.data(), frame, CAM_W * CAM_H * 4);
            m_frameSeq.fetch_add(1);
        }
        m_frameDirty.store(true);
    }
#endif
}

// Decode thread: continuously decodes the most recent frame. Runs independently
// of the camera read so a slow decode never freezes the preview, and always
// works on the freshest frame (no stale backlog - the cause of "won't scan
// until I close and reopen").
void QrScannerActivity::decodeLoop() {
#ifdef __vita__
    uint32_t lastSeq = 0;
    while (m_running.load()) {
        // Wait for a frame newer than the one we last decoded
        uint32_t seq = m_frameSeq.load();
        if (seq == lastSeq || !m_qr) {
            sceKernelDelayThread(8 * 1000);  // 8ms
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            std::memcpy(m_decodeRgba.data(), m_frameRgba.data(), CAM_W * CAM_H * 4);
            lastSeq = m_frameSeq.load();
        }

        const unsigned char* frame = m_decodeRgba.data();
        int qw = 0, qh = 0;
        uint8_t* gray = quirc_begin(m_qr, &qw, &qh);
        if (!gray) continue;
        const int n = CAM_W * CAM_H;
        for (int i = 0; i < n; i++) {
            const unsigned char* px = frame + i * 4;
            // Fast luma approximation: (R + 2G + B) / 4
            gray[i] = (uint8_t)((px[0] + (px[1] << 1) + px[2]) >> 2);
        }
        quirc_end(m_qr);

        int count = quirc_count(m_qr);
        for (int i = 0; i < count; i++) {
            struct quirc_code code;
            struct quirc_data data;
            quirc_extract(m_qr, i, &code);
            if (quirc_decode(&code, &data) != QUIRC_SUCCESS) continue;

            std::string payload(reinterpret_cast<char*>(data.payload),
                                (size_t)data.payload_len);
            if (payload.empty()) continue;

            if (!m_resultDelivered.exchange(true)) {
                brls::Logger::info("QR: decoded {} bytes", payload.size());
                m_running.store(false);
                auto cb = m_onResult;
                brls::sync([cb, payload]() {
                    brls::Application::popActivity(
                        brls::TransitionAnimation::FADE, [cb, payload]() {
                            if (cb) cb(payload);
                        });
                });
            }
            return;
        }
    }
#endif
}

void QrScannerActivity::stopCamera() {
#ifdef __vita__
    if (!m_running.load() && !m_cameraOpen) {
        if (m_camThread.joinable()) m_camThread.join();
        if (m_decodeThread.joinable()) m_decodeThread.join();
        return;
    }
    m_running.store(false);
    // Closing the camera unblocks a pending blocking read
    if (m_cameraOpen) {
        sceCameraStop(m_camDevice);
    }
    if (m_camThread.joinable()) m_camThread.join();
    if (m_decodeThread.joinable()) m_decodeThread.join();
    if (m_cameraOpen) {
        sceCameraClose(m_camDevice);
        m_cameraOpen = false;
    }
    if (m_memblock >= 0) {
        sceKernelFreeMemBlock(m_memblock);
        m_memblock = -1;
        m_camBase = nullptr;
    }
#endif
}

} // namespace vita_ma
