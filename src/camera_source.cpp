#include "lifeguard/camera_source.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include <opencv2/videoio.hpp>

#ifdef LIFEGUARD_QNX_CAMERA
#include <atomic>
#include <condition_variable>
#include <mutex>

#include <camera/camera_api.h>
#include <screen/screen.h>
#include <opencv2/imgproc.hpp>
#endif

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
        // Resolve the source: "file" uses video_file; any other backend
        // (host webcam testing) uses camera_device, which may be a numeric
        // OpenCV camera index (e.g. "0") or a file path / URL.
        const std::string src = (cfg_.camera_backend == "file")
                                    ? cfg_.video_file
                                    : cfg_.camera_device;
        if (src.empty()) {
            std::fprintf(stderr, "[camera:file] no source set "
                                 "(video_file / camera_device is empty)\n");
            return false;
        }

        const bool is_index =
            src.find_first_not_of("0123456789") == std::string::npos;
        const bool ok = is_index ? cap_.open(std::stoi(src)) : cap_.open(src);
        if (!ok) {
            std::fprintf(stderr, "[camera:file] cannot open %s\n", src.c_str());
            return false;
        }

        if (is_index) {  // live device: request the configured capture format
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

// -------------------------------------------------------------------------
// QNX Sensor Framework (QSF) backend — the real on-target capture path.
//
// Adapted from the QNX "ai-camera-app" sample
// (https://gitlab.com/qnx/projects/ai-camera-app). It drives the camera
// viewfinder and receives buffer-ready notifications as QNX pulses on a
// private channel, converting each camera buffer into a BGR cv::Mat.
//
// The same QSF path serves both a USB/UVC camera and the Raspberry Pi Camera
// Module 3 — the Sensor Framework abstracts the sensor. Select the unit with
// `camera_unit` in the config.
//
// Requires QNX SDP 8.0 Sensor Framework packages (camapi, sf.base, mm.aoi,
// mmf.core) and the camera configured in the target's sensor startup.
// Guarded by LIFEGUARD_QNX_CAMERA so the project still builds off-target.
// -------------------------------------------------------------------------
#ifdef LIFEGUARD_QNX_CAMERA

namespace {

// Pull width/height/format helpers out of the camera_buffer_t union.
int bufWidth(const camera_buffer_t& b) {
    switch (b.frametype) {
        case CAMERA_FRAMETYPE_NV12:    return b.framedesc.nv12.width;
        case CAMERA_FRAMETYPE_BGR8888: return b.framedesc.bgr8888.width;
        case CAMERA_FRAMETYPE_CBYCRY:  return b.framedesc.cbycry.width;
        case CAMERA_FRAMETYPE_YCBYCR:  return b.framedesc.ycbycr.width;
        default: return -1;
    }
}
int bufHeight(const camera_buffer_t& b) {
    switch (b.frametype) {
        case CAMERA_FRAMETYPE_NV12:    return b.framedesc.nv12.height;
        case CAMERA_FRAMETYPE_BGR8888: return b.framedesc.bgr8888.height;
        case CAMERA_FRAMETYPE_CBYCRY:  return b.framedesc.cbycry.height;
        case CAMERA_FRAMETYPE_YCBYCR:  return b.framedesc.ycbycr.height;
        default: return -1;
    }
}

// Convert a camera buffer into a BGR cv::Mat (deep copy so the buffer can be
// returned immediately).
bool bufferToBgr(const camera_buffer_t& b, cv::Mat& out) {
    const int w = bufWidth(b);
    const int h = bufHeight(b);
    if (w <= 0 || h <= 0) return false;

    switch (b.frametype) {
        case CAMERA_FRAMETYPE_NV12: {
            // QNX describes NV12 as two semiplanar planes. Copy row-by-row
            // because the IMX708 driver is allowed to pad either plane.
            const auto* base = static_cast<const uint8_t*>(b.framebuf);
            if (!base ||
                b.framedesc.nv12.stride < static_cast<uint32_t>(w) ||
                b.framedesc.nv12.uv_stride < static_cast<uint32_t>(w) ||
                b.framedesc.nv12.uv_offset <= 0) {
                return false;
            }

            cv::Mat nv12(h + h / 2, w, CV_8UC1);
            for (int row = 0; row < h; ++row) {
                std::memcpy(nv12.ptr(row),
                            base + row * b.framedesc.nv12.stride, w);
            }
            const auto* uv = base + b.framedesc.nv12.uv_offset;
            for (int row = 0; row < h / 2; ++row) {
                std::memcpy(nv12.ptr(h + row),
                            uv + row * b.framedesc.nv12.uv_stride, w);
            }
            cv::cvtColor(nv12, out, cv::COLOR_YUV2BGR_NV12);
            return true;
        }
        case CAMERA_FRAMETYPE_CBYCRY: {  // packed UYVY
            cv::Mat yuv(h, w, CV_8UC2, b.framebuf, w * 2);
            cv::cvtColor(yuv, out, cv::COLOR_YUV2BGR_UYVY);
            return true;
        }
        case CAMERA_FRAMETYPE_YCBYCR: {  // packed YUY2
            cv::Mat yuv(h, w, CV_8UC2, b.framebuf, w * 2);
            cv::cvtColor(yuv, out, cv::COLOR_YUV2BGR_YUY2);
            return true;
        }
        case CAMERA_FRAMETYPE_BGR8888: {  // BGRX
            cv::Mat bgra(h, w, CV_8UC4, b.framebuf, w * 4);
            cv::cvtColor(bgra, out, cv::COLOR_BGRA2BGR);
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

class QnxCameraSource : public CameraSource {
public:
    explicit QnxCameraSource(const Config& cfg) : cfg_(cfg) {}
    ~QnxCameraSource() override { close(); }

    bool open() override {
        const camera_unit_t unit =
            static_cast<camera_unit_t>(cfg_.camera_unit);

        if (!ensureScreenWindow()) {
            std::fprintf(stderr,
                         "[camera:qsf] failed to create Screen window\n");
            return false;
        }

        std::fprintf(stderr, "[camera:qsf] opening unit=%d mode=RW\n",
                     cfg_.camera_unit);
        camera_error_t rc = camera_open(unit, CAMERA_MODE_RW, &handle_);
        if (rc != CAMERA_EOK) {
            std::fprintf(stderr, "[camera:qsf] camera_open(unit=%d) failed: %d\n",
                         cfg_.camera_unit, rc);
            return false;
        }

        // Let QNX create the viewfinder window, matching the sample flow.
        rc = camera_set_vf_property(handle_, CAMERA_IMGPROP_CREATEWINDOW, 1);
        if (rc != CAMERA_EOK) {
            std::fprintf(stderr,
                         "[camera:qsf] create-window enable failed: %d "
                         "(continuing)\n",
                         rc);
        }

        // Keep the QNX callback path: the sensor config owns format/size.
        running_.store(true);
        rc = camera_start_viewfinder(handle_, viewfinderCallback, nullptr, this);
        if (rc != CAMERA_EOK) {
            running_.store(false);
            std::fprintf(stderr,
                         "[camera:qsf] start_viewfinder failed: %d "
                         "(unit=%d, mode=RW)\n",
                         rc, cfg_.camera_unit);
            return false;
        }

        viewfinder_started_ = true;
        return true;
    }

    bool read(Frame& out) override {
        std::unique_lock<std::mutex> lock(mtx_);
        // Wait for a frame newer than the last one we handed out.
        cv_.wait(lock, [this] {
            return !running_.load() || latest_id_ > delivered_id_;
        });
        if (!running_.load() && latest_id_ <= delivered_id_) return false;

        out.image = latest_.clone();
        out.timestamp_ns = latest_ts_;
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
        cv_.notify_all();

        if (handle_ != CAMERA_HANDLE_INVALID) {
            camera_close(handle_);
            handle_ = CAMERA_HANDLE_INVALID;
        }
        destroyScreenWindow();
    }

private:
    bool ensureScreenWindow() {
        if (screen_window_ != nullptr) return true;

        if (screen_create_context(&screen_ctx_, SCREEN_APPLICATION_CONTEXT) != 0) {
            std::fprintf(stderr, "[camera:qsf] screen_create_context failed\n");
            return false;
        }
        if (screen_create_window(&screen_window_, screen_ctx_) != 0) {
            std::fprintf(stderr, "[camera:qsf] screen_create_window failed\n");
            destroyScreenWindow();
            return false;
        }

        int size[2] = {cfg_.frame_width, cfg_.frame_height};
        int visible = 1;
        int color = SCREEN_FORMAT_RGBA8888;

        screen_set_window_property_iv(screen_window_, SCREEN_PROPERTY_SIZE, size);
        screen_set_window_property_iv(screen_window_, SCREEN_PROPERTY_BUFFER_SIZE, size);
        screen_set_window_property_iv(screen_window_, SCREEN_PROPERTY_FORMAT, &color);
        screen_set_window_property_iv(screen_window_, SCREEN_PROPERTY_VISIBLE, &visible);
        screen_create_window_buffers(screen_window_, 1);
        return true;
    }

    void destroyScreenWindow() {
        if (screen_window_ != nullptr) {
            screen_destroy_window(screen_window_);
            screen_window_ = nullptr;
        }
        if (screen_ctx_ != nullptr) {
            screen_destroy_context(screen_ctx_);
            screen_ctx_ = nullptr;
        }
    }

    static void viewfinderCallback(camera_handle_t /*handle*/,
                                   camera_buffer_t* buffer, void* arg) {
        if (!buffer || !arg) return;
        static_cast<QnxCameraSource*>(arg)->onViewfinderBuffer(*buffer);
    }

    void onViewfinderBuffer(const camera_buffer_t& cbuf) {
        if (!running_.load()) return;

        cv::Mat bgr;
        if (!bufferToBgr(cbuf, bgr)) return;

        std::lock_guard<std::mutex> lock(mtx_);
        latest_ = std::move(bgr);
        latest_ts_ = nowNs();
        latest_id_ = static_cast<uint64_t>(cbuf.frametimestamp);
        if (latest_id_ <= delivered_id_) latest_id_ = delivered_id_ + 1;
        cv_.notify_one();
    }

    const Config& cfg_;
    camera_handle_t handle_ = CAMERA_HANDLE_INVALID;
    screen_context_t screen_ctx_ = nullptr;
    screen_window_t screen_window_ = nullptr;
    std::atomic<bool> running_{false};
    bool viewfinder_started_ = false;
    std::mutex mtx_;
    std::condition_variable cv_;
    cv::Mat latest_;
    uint64_t latest_ts_ = 0;
    uint64_t latest_id_ = 0;
    uint64_t delivered_id_ = 0;
};

#endif  // LIFEGUARD_QNX_CAMERA

}  // namespace

std::unique_ptr<CameraSource> CameraSource::create(const Config& cfg) {
    if (cfg.camera_backend == "file") {
        return std::make_unique<FileCameraSource>(cfg);
    }
#ifdef LIFEGUARD_QNX_CAMERA
    // "uvc", "csi", or "qsf" all use the Sensor Framework capture path; the
    // framework abstracts the underlying USB or CSI (Camera Module 3) sensor.
    return std::make_unique<QnxCameraSource>(cfg);
#else
    std::fprintf(stderr,
                 "[camera] backend '%s' has no QNX build "
                 "(-DLIFEGUARD_QNX_CAMERA=ON); using OpenCV on the host. Set "
                 "camera_device to a webcam index (e.g. 0) or a file path.\n",
                 cfg.camera_backend.c_str());
    return std::make_unique<FileCameraSource>(cfg);
#endif
}

}  // namespace lifeguard
