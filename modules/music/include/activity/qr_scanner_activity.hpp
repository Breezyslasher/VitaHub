/**
 * Vita Music Assistant - QR Scanner Activity
 * Live camera preview + quirc QR decoding, used to scan Remote Access IDs.
 */

#pragma once

#include <borealis.hpp>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct quirc;

namespace vita_ma {

class QrScannerActivity : public brls::Activity {
public:
    // onResult receives the raw decoded QR payload (called on the UI thread
    // after the scanner closes itself)
    explicit QrScannerActivity(std::function<void(const std::string&)> onResult);
    ~QrScannerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;
    void willDisappear(bool resetState) override;

private:
    static constexpr int CAM_W = 640;
    static constexpr int CAM_H = 480;

    bool startCamera();
    void stopCamera();
    void cameraLoop();   // reads frames + updates preview (kept light so preview stays smooth)
    void decodeLoop();   // decodes the latest frame on its own thread (quirc is heavy)

    std::function<void(const std::string&)> m_onResult;

    // Latest camera frame (RGBA bytes), shared by preview + decoder.
    // m_frameSeq bumps on each captured frame so the decoder only re-decodes
    // fresh frames and never works on a stale backlog.
    std::mutex m_frameMutex;
    std::vector<unsigned char> m_frameRgba;
    std::atomic<bool> m_frameDirty{false};
    std::atomic<uint32_t> m_frameSeq{0};
    std::vector<unsigned char> m_decodeRgba;  // decoder's private snapshot

    std::thread m_camThread;
    std::thread m_decodeThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_resultDelivered{false};

    quirc* m_qr = nullptr;

#ifdef __vita__
    int m_memblock = -1;        // SceUID of the CDRAM camera buffer
    void* m_camBase = nullptr;  // mapped base of the camera buffer
    bool m_cameraOpen = false;
    int m_camDevice = 1;        // SCE_CAMERA_DEVICE_BACK by default
#endif

    // Custom preview view (defined in the .cpp); owns the NVG texture
    friend class CameraPreviewView;
    int m_previewTex = 0;

    BRLS_BIND(brls::Box, previewHolder, "qr/preview_holder");
    BRLS_BIND(brls::Label, statusLabel, "qr/status");
};

} // namespace vita_ma
