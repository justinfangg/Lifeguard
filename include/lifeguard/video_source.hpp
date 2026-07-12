#pragma once

#include <memory>

#include "lifeguard/config.hpp"
#include "lifeguard/frame.hpp"

namespace lifeguard {

// Abstract video source. The only backend is a file reader (FileVideoSource)
// that decodes frames from a recorded video file (e.g. an MP4) via OpenCV.
// AI Lifeguard runs entirely on recorded footage — there is no live camera.
class VideoSource {
public:
    virtual ~VideoSource() = default;

    // Open the video file. Returns false on failure.
    virtual bool open() = 0;

    // Block until the next frame is available. Returns false on
    // end-of-stream or unrecoverable error.
    virtual bool read(Frame& out) = 0;

    virtual void close() = 0;

    // Factory: create the file video source from config.video_file.
    static std::unique_ptr<VideoSource> create(const Config& cfg);
};

}  // namespace lifeguard
