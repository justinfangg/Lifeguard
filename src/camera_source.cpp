#include "lifeguard/camera_source.hpp"

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

class OpenCvCameraSource : public CameraSource {
public:
    explicit OpenCvCameraSource(const Config& cfg) : cfg_(cfg) {}

    bool open() override {
        const std::string src =
            cfg_.camera_backend == "file" ? cfg_.video_file : cfg_.camera_device;
        if (src.empty()) {
            std::fprintf(stderr, "[camera] no source set\n");
            return false;
        }

        const bool index =
            src.find_first_not_of("0123456789") == std::string::npos;
        const bool ok = index ? cap_.open(std::stoi(src)) : cap_.open(src);
        if (!ok) {
            std::fprintf(stderr, "[camera] cannot open %s\n", src.c_str());
            return false;
        }

        if (index) {
            cap_.set(cv::CAP_PROP_FRAME_WIDTH, cfg_.frame_width);
            cap_.set(cv::CAP_PROP_FRAME_HEIGHT, cfg_.frame_height);
            cap_.set(cv::CAP_PROP_FPS, cfg_.target_fps);
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

}  // namespace

std::unique_ptr<CameraSource> CameraSource::create(const Config& cfg) {
    return std::make_unique<OpenCvCameraSource>(cfg);
}

}  // namespace lifeguard
