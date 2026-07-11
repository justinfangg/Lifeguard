#pragma once

#include <memory>
#include <string>

#include "lifeguard/config.hpp"
#include "lifeguard/frame.hpp"

namespace lifeguard {

// Abstract video source. Concrete backends:
//   * UvcCameraSource  — USB/UVC webcam via the QNX camera framework (v1)
//   * CsiCameraSource  — MIPI CSI Camera Module 3 / IMX708 (later)
//   * FileCameraSource — read frames from a video file (dev / testing)
class CameraSource {
public:
    virtual ~CameraSource() = default;

    // Open the device / file. Returns false on failure.
    virtual bool open() = 0;

    // Block until the next frame is available. Returns false on
    // end-of-stream or unrecoverable error.
    virtual bool read(Frame& out) = 0;

    virtual void close() = 0;

    // Factory: pick a backend from config.camera_backend.
    static std::unique_ptr<CameraSource> create(const Config& cfg);
};

}  // namespace lifeguard
