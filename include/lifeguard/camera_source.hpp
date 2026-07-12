#pragma once

#include <memory>

#include "lifeguard/config.hpp"
#include "lifeguard/frame.hpp"

namespace lifeguard {

// Captures frames from either a recorded file or a live OpenCV camera.
class CameraSource {
public:
    virtual ~CameraSource() = default;

    virtual bool open() = 0;
    virtual bool read(Frame& out) = 0;
    virtual void close() = 0;

    static std::unique_ptr<CameraSource> create(const Config& cfg);
};

}  // namespace lifeguard
