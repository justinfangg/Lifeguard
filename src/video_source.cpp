#include "lifeguard/video_source.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include <opencv2/videoio.hpp>

namespace lifeguard {

namespace {

uint64_t nowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
        .count();
}

// -------------------------------------------------------------------------
// File video source — reads frames from a recorded video file (e.g. an MP4)
// via OpenCV. This is the only capture backend: AI Lifeguard runs entirely on
// recorded footage.
// -------------------------------------------------------------------------
class FileVideoSource : public VideoSource {
public:
    explicit FileVideoSource(const Config& cfg) : cfg_(cfg) {}

    bool open() override {
        if (cfg_.video_file.empty()) {
            std::fprintf(stderr, "[video] no input set (video_file is empty)\n");
            return false;
        }
        if (!cap_.open(cfg_.video_file)) {
            std::fprintf(stderr, "[video] cannot open %s\n",
                         cfg_.video_file.c_str());
            return false;
        }
        return true;
    }

    bool read(Frame& out) override {
        if (!cap_.read(out.image) || out.image.empty()) return false;  // EOS
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

}  // namespace

std::unique_ptr<VideoSource> VideoSource::create(const Config& cfg) {
    return std::make_unique<FileVideoSource>(cfg);
}

}  // namespace lifeguard
