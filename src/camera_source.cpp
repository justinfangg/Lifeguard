#include "lifeguard/camera_source.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <opencv2/videoio.hpp>

#ifdef LIFEGUARD_QNX_CAMERA
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unistd.h>
#include <vector>

#include <camera/camera_api.h>
#include <opencv2/imgproc.hpp>
#endif

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
        source_ = cfg_.camera_backend == "file" ? cfg_.video_file
                                                : cfg_.camera_device;
        if (source_.empty()) {
            std::fprintf(stderr, "[camera:opencv] no input source configured\n");
            return false;
        }

        const bool ok = openCapture();
        if (!ok) {
            std::fprintf(stderr, "[camera:opencv] cannot open %s\n",
                         source_.c_str());
            return false;
        }
        std::fprintf(stderr, "[camera:opencv] opened %s (%dx%d @ %.1f fps)\n",
                     source_.c_str(),
                     static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH)),
                     static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT)),
                     cap_.get(cv::CAP_PROP_FPS));
        return true;
    }

    bool read(Frame& out) override {
        if (cap_.read(out.image) && !out.image.empty()) {
            out.timestamp_ns = nowNs();
            out.index = index_++;
            consecutive_failures_ = 0;
            return true;
        }

        // Network cameras can briefly disconnect. Re-open a few times instead
        // of immediately killing the inference process. Files still terminate
        // normally at end-of-stream.
        if (cfg_.camera_backend != "network" &&
            source_.find("://") == std::string::npos) {
            return false;
        }
        ++consecutive_failures_;
        if (consecutive_failures_ > 5) return false;
        std::fprintf(stderr, "[camera:opencv] stream interrupted; reconnecting (%d/5)\n",
                     consecutive_failures_);
        cap_.release();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return openCapture() && read(out);
    }

    void close() override { cap_.release(); }

private:
    bool openCapture() {
        const bool index = !source_.empty() &&
            source_.find_first_not_of("0123456789") == std::string::npos;
        const bool network = source_.find("://") != std::string::npos;
        bool ok = false;
        if (index) {
            ok = cap_.open(std::stoi(source_));
        } else if (network) {
            ok = cap_.open(source_, cv::CAP_FFMPEG,
                           {cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
                            cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000});
        } else {
            ok = cap_.open(source_);
        }
        if (!ok) return false;
        cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
        if (index) {
            cap_.set(cv::CAP_PROP_FRAME_WIDTH, cfg_.frame_width);
            cap_.set(cv::CAP_PROP_FRAME_HEIGHT, cfg_.frame_height);
            cap_.set(cv::CAP_PROP_FPS, cfg_.target_fps);
        }
        return true;
    }

    const Config& cfg_;
    cv::VideoCapture cap_;
    std::string source_;
    uint64_t index_ = 0;
    int consecutive_failures_ = 0;
};

#ifdef LIFEGUARD_QNX_CAMERA

int bufferWidth(const camera_buffer_t& b) {
    switch (b.frametype) {
        case CAMERA_FRAMETYPE_NV12: return b.framedesc.nv12.width;
        case CAMERA_FRAMETYPE_BGR8888: return b.framedesc.bgr8888.width;
        case CAMERA_FRAMETYPE_CBYCRY: return b.framedesc.cbycry.width;
        case CAMERA_FRAMETYPE_YCBYCR: return b.framedesc.ycbycr.width;
        default: return -1;
    }
}

int bufferHeight(const camera_buffer_t& b) {
    switch (b.frametype) {
        case CAMERA_FRAMETYPE_NV12: return b.framedesc.nv12.height;
        case CAMERA_FRAMETYPE_BGR8888: return b.framedesc.bgr8888.height;
        case CAMERA_FRAMETYPE_CBYCRY: return b.framedesc.cbycry.height;
        case CAMERA_FRAMETYPE_YCBYCR: return b.framedesc.ycbycr.height;
        default: return -1;
    }
}

bool bufferToBgr(const camera_buffer_t& b, cv::Mat& out,
                 int configured_width, int configured_height) {
    int width = bufferWidth(b);
    int height = bufferHeight(b);
    if (width <= 0 || height <= 0) {
        width = configured_width;
        height = configured_height;
    }
    if (width <= 0 || height <= 0 || !b.framebuf) return false;

    switch (b.frametype) {
        case CAMERA_FRAMETYPE_NV12: {
            const auto* base = static_cast<const uint8_t*>(b.framebuf);
            if (b.framedesc.nv12.stride < static_cast<uint32_t>(width) ||
                b.framedesc.nv12.uv_stride < static_cast<uint32_t>(width) ||
                b.framedesc.nv12.uv_offset <= 0) return false;
            cv::Mat nv12(height + height / 2, width, CV_8UC1);
            for (int row = 0; row < height; ++row) {
                std::memcpy(nv12.ptr(row),
                            base + row * b.framedesc.nv12.stride, width);
            }
            const auto* uv = base + b.framedesc.nv12.uv_offset;
            for (int row = 0; row < height / 2; ++row) {
                std::memcpy(nv12.ptr(height + row),
                            uv + row * b.framedesc.nv12.uv_stride, width);
            }
            cv::cvtColor(nv12, out, cv::COLOR_YUV2BGR_NV12);
            return true;
        }
        case CAMERA_FRAMETYPE_CBYCRY: {
            cv::Mat yuv(height, width, CV_8UC2, b.framebuf, width * 2);
            cv::cvtColor(yuv, out, cv::COLOR_YUV2BGR_UYVY);
            return true;
        }
        case CAMERA_FRAMETYPE_YCBYCR: {
            cv::Mat yuv(height, width, CV_8UC2, b.framebuf, width * 2);
            cv::cvtColor(yuv, out, cv::COLOR_YUV2BGR_YUY2);
            return true;
        }
        case CAMERA_FRAMETYPE_BGR8888: {
            cv::Mat bgra(height, width, CV_8UC4, b.framebuf, width * 4);
            cv::cvtColor(bgra, out, cv::COLOR_BGRA2BGR);
            return true;
        }
        case CAMERA_FRAMETYPE_RGB888: {
            cv::Mat rgb(height, width, CV_8UC3, b.framebuf, width * 3);
            cv::cvtColor(rgb, out, cv::COLOR_RGB2BGR);
            return true;
        }
        case CAMERA_FRAMETYPE_RGB8888: {
            const int from_to[] = {0, 2, 1, 1, 2, 0, 3, 3};
            cv::Mat argb(height, width, CV_8UC4, b.framebuf, width * 4);
            cv::Mat bgra(argb.size(), argb.type());
            cv::mixChannels(&argb, 1, &bgra, 1, from_to, 4);
            cv::cvtColor(bgra, out, cv::COLOR_RGBA2RGB);
            return true;
        }
        default: return false;
    }
}

// This is the exact QSF callback flow that was verified against the Pi 5
// Camera Module 3 (IMX708). The Pi sender uses it; TensorFlow stays on host.
class QnxCameraSource : public CameraSource {
public:
    explicit QnxCameraSource(const Config& cfg) : cfg_(cfg) {}
    ~QnxCameraSource() override { close(); }

    bool open() override {
        const camera_unit_t unit = static_cast<camera_unit_t>(cfg_.camera_unit);
        unsigned int count = 0;
        camera_error_t rc = CAMERA_EOK;
        for (int attempt = 0; attempt < 1600; ++attempt) {
            rc = camera_get_supported_cameras(0, &count, nullptr);
            if (rc != CAMERA_EOK) {
                std::fprintf(stderr, "[camera:qsf] get-supported-cameras failed: %d\n", rc);
                return false;
            }
            if (count != 0) break;
            usleep(5000);
        }
        if (count == 0) {
            std::fprintf(stderr, "[camera:qsf] no camera published by sensor after 8s\n");
            return false;
        }

        std::vector<camera_unit_t> units(count);
        unsigned int returned = count;
        rc = camera_get_supported_cameras(count, &returned, units.data());
        if (rc != CAMERA_EOK) return false;
        units.resize(returned);
        bool found = false;
        std::fprintf(stderr, "[camera:qsf] published units:");
        for (camera_unit_t published : units) {
            std::fprintf(stderr, " %d", static_cast<int>(published));
            found = found || published == unit;
        }
        std::fprintf(stderr, " (requested=%d)\n", cfg_.camera_unit);
        if (!found) return false;

        const auto mode = CAMERA_MODE_RO | CAMERA_MODE_ROLL;
        camera_handle_t probe = CAMERA_HANDLE_INVALID;
        rc = camera_open(unit, mode, &probe);
        if (rc != CAMERA_EOK) return false;
        camera_frametype_t format = CAMERA_FRAMETYPE_RGB888;
        unsigned int width = 0, height = 0;
        double fps = 0.0;
        rc = camera_get_vf_property(probe, CAMERA_IMGPROP_FORMAT, &format,
                                    CAMERA_IMGPROP_WIDTH, &width,
                                    CAMERA_IMGPROP_HEIGHT, &height,
                                    CAMERA_IMGPROP_FRAMERATE, &fps);
        camera_close(probe);
        if (rc != CAMERA_EOK) return false;
        viewfinder_width_ = static_cast<int>(width);
        viewfinder_height_ = static_cast<int>(height);
        std::fprintf(stderr,
                     "[camera:qsf] unit=%d viewfinder: %ux%u @ %.3f, format=%d\n",
                     cfg_.camera_unit, width, height, fps,
                     static_cast<int>(format));

        rc = camera_open(unit, mode, &handle_);
        if (rc != CAMERA_EOK) return false;
        rc = camera_set_vf_property(handle_, CAMERA_IMGPROP_CREATEWINDOW, false);
        if (rc != CAMERA_EOK) {
            camera_close(handle_);
            handle_ = CAMERA_HANDLE_INVALID;
            return false;
        }
        running_.store(true);
        rc = camera_start_viewfinder(handle_, viewfinderCallback,
                                     statusCallback, this);
        if (rc != CAMERA_EOK) {
            running_.store(false);
            camera_close(handle_);
            handle_ = CAMERA_HANDLE_INVALID;
            return false;
        }
        viewfinder_started_ = true;
        return true;
    }

    bool read(Frame& out) override {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] {
            return !running_.load() || latest_id_ > delivered_id_;
        });
        if (!running_.load() && latest_id_ <= delivered_id_) return false;
        out.image = latest_.clone();
        out.timestamp_ns = latest_timestamp_;
        out.index = latest_id_;
        delivered_id_ = latest_id_;
        return !out.image.empty();
    }

    void close() override {
        running_.store(false);
        if (handle_ != CAMERA_HANDLE_INVALID && viewfinder_started_) {
            camera_stop_viewfinder(handle_);
            viewfinder_started_ = false;
        }
        ready_.notify_all();
        if (handle_ != CAMERA_HANDLE_INVALID) {
            camera_close(handle_);
            handle_ = CAMERA_HANDLE_INVALID;
        }
    }

private:
    static void viewfinderCallback(camera_handle_t, camera_buffer_t* buffer,
                                   void* arg) {
        if (buffer && arg) static_cast<QnxCameraSource*>(arg)->onFrame(*buffer);
    }
    static void statusCallback(camera_handle_t handle, camera_devstatus_t status,
                               uint16_t extra, void*) {
        std::fprintf(stderr, "[camera:qsf] status: handle=%d status=%d extra=%u\n",
                     static_cast<int>(handle), static_cast<int>(status), extra);
    }
    void onFrame(const camera_buffer_t& buffer) {
        if (!running_.load()) return;
        cv::Mat bgr;
        if (!bufferToBgr(buffer, bgr, viewfinder_width_, viewfinder_height_)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::move(bgr);
        latest_timestamp_ = nowNs();
        latest_id_ = static_cast<uint64_t>(buffer.frametimestamp);
        if (latest_id_ <= delivered_id_) latest_id_ = delivered_id_ + 1;
        ready_.notify_one();
    }

    const Config& cfg_;
    camera_handle_t handle_ = CAMERA_HANDLE_INVALID;
    std::atomic<bool> running_{false};
    bool viewfinder_started_ = false;
    std::mutex mutex_;
    std::condition_variable ready_;
    cv::Mat latest_;
    int viewfinder_width_ = 0;
    int viewfinder_height_ = 0;
    uint64_t latest_timestamp_ = 0;
    uint64_t latest_id_ = 0;
    uint64_t delivered_id_ = 0;
};
#endif

}  // namespace

std::unique_ptr<CameraSource> CameraSource::create(const Config& cfg) {
#ifdef LIFEGUARD_QNX_CAMERA
    if (cfg.camera_backend == "csi" || cfg.camera_backend == "qsf") {
        return std::make_unique<QnxCameraSource>(cfg);
    }
#endif
    return std::make_unique<OpenCvCameraSource>(cfg);
}

}  // namespace lifeguard
