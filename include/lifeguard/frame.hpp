#pragma once

#include <cstdint>
#include <opencv2/core.hpp>

namespace lifeguard {

// A single captured video frame plus a monotonic timestamp (nanoseconds).
struct Frame {
    cv::Mat image;          // BGR, host memory
    uint64_t timestamp_ns = 0;
    uint64_t index = 0;     // monotonically increasing frame counter

    bool empty() const { return image.empty(); }
};

}  // namespace lifeguard
