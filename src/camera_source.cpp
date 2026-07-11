#include "lifeguard/camera_source.hpp"

#include <chrono>
#include <cstdio>

#include <opencv2/videoio.hpp>

namespace lifeguard {

namespace {

uint64_t nowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
        .count();
}

// -------------------------------------------------------------------------
// File / test backend — reads frames from a video file via OpenCV. Useful for
// developing the whole pipeline off-target against recorded pool footage.
// -------------------------------------------------------------------------
class FileCameraSource : public CameraSource {
public:
    explicit FileCameraSource(const Config& cfg) : cfg_(cfg) {}

    bool open() override {
        if (!cap_.open(cfg_.video_file)) {
            std::fprintf(stderr, "[camera:file] cannot open %s\n",
                         cfg_.video_file.c_str());
            return false;
        }
        return true;
    }

    bool read(Frame& out) override {
        if (!cap_.read(out.image) || out.image.empty()) return false;
        out.timestamp_ns = nowNs();
        out.index = index_++;
        return true;
    }

    void close() override { cap_.release(); }

private:
    const Config& cfg_;
    cv::VideoCapture cap_;
    uint64_t index_ = 0;
};

// -------------------------------------------------------------------------
// UVC (USB webcam) backend — recommended for v1 on QNX. Implemented against
// the QNX camera framework (camapi). Guarded by LIFEGUARD_QNX_CAMERA so the
// project builds off-target before bring-up.
// -------------------------------------------------------------------------
class UvcCameraSource : public CameraSource {
public:
    explicit UvcCameraSource(const Config& cfg) : cfg_(cfg) {}

    bool open() override {
#ifdef LIFEGUARD_QNX_CAMERA
        // TODO(bring-up): open a UVC unit with the QNX camera framework.
        //   camera_get_supported_cameras(...) to enumerate units
        //   camera_open(unit, CAMERA_MODE_RW, &handle_)
        //   camera_set_vf_property(handle_, width, height, format)
        //   camera_start_viewfinder(handle_, &frameCallback, nullptr, this)
        // Feed frames from the callback into a small queue that read() drains.
        std::fprintf(stderr, "[camera:uvc] TODO: implement camapi capture\n");
        return false;
#else
        std::fprintf(stderr, "[camera:uvc] built without LIFEGUARD_QNX_CAMERA; "
                             "rebuild with -DLIFEGUARD_QNX_CAMERA=ON on target\n");
        return false;
#endif
    }

    bool read(Frame& /*out*/) override { return false; }
    void close() override {}

private:
    const Config& cfg_;
};

// -------------------------------------------------------------------------
// CSI backend — Raspberry Pi Camera Module 3 (IMX708 over MIPI CSI-2).
// This requires a QNX MIPI CSI capture path for the Pi 5 / RP1, which is
// non-trivial and may need BSP/driver work. Deferred until the UVC path works.
// -------------------------------------------------------------------------
class CsiCameraSource : public CameraSource {
public:
    explicit CsiCameraSource(const Config& cfg) : cfg_(cfg) {}

    bool open() override {
        // TODO(bring-up): MIPI CSI-2 IMX708 capture on QNX (RP1 image pipeline).
        std::fprintf(stderr, "[camera:csi] not implemented; use UVC for v1\n");
        return false;
    }
    bool read(Frame& /*out*/) override { return false; }
    void close() override {}

private:
    const Config& cfg_;
};

}  // namespace

std::unique_ptr<CameraSource> CameraSource::create(const Config& cfg) {
    if (cfg.camera_backend == "file") {
        return std::make_unique<FileCameraSource>(cfg);
    }
    if (cfg.camera_backend == "csi") {
        return std::make_unique<CsiCameraSource>(cfg);
    }
    // Default: UVC.
    return std::make_unique<UvcCameraSource>(cfg);
}

}  // namespace lifeguard
