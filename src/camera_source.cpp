#include "lifeguard/camera_source.hpp"

#include <chrono>
#include <cstdio>

#include <opencv2/videoio.hpp>

#ifdef LIFEGUARD_QNX_CAMERA
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <camera/camera_api.h>
#include <opencv2/imgproc.hpp>
#include <screen/screen.h>
#include <sys/neutrino.h>
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

constexpr int kPulseBufferAvailable = _PULSE_CODE_MINAVAIL + 1;
constexpr int kPulseStop = _PULSE_CODE_MINAVAIL + 2;

// Pull width/height/format helpers out of the camera_buffer_t union.
int bufWidth(const camera_buffer_t& b) {
    switch (b.frametype) {
        case CAMERA_FRAMETYPE_BGR8888: return b.framedesc.bgr8888.width;
        case CAMERA_FRAMETYPE_CBYCRY:  return b.framedesc.cbycry.width;
        case CAMERA_FRAMETYPE_YCBYCR:  return b.framedesc.ycbycr.width;
        default: return -1;
    }
}
int bufHeight(const camera_buffer_t& b) {
    switch (b.frametype) {
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
        camera_error_t rc = camera_open(unit, CAMERA_MODE_RW, &handle_);
        if (rc != CAMERA_EOK) {
            std::fprintf(stderr, "[camera:qsf] camera_open(unit=%d) failed: %d\n",
                         cfg_.camera_unit, rc);
            return false;
        }

        // Request the viewfinder resolution. Format is negotiated by the
        // sensor; bufferToBgr() handles CBYCRY/YCBYCR/BGR8888.
        rc = camera_set_vf_property(handle_,
                                    CAMERA_IMGPROP_WIDTH, cfg_.frame_width,
                                    CAMERA_IMGPROP_HEIGHT, cfg_.frame_height);
        if (rc != CAMERA_EOK) {
            std::fprintf(stderr, "[camera:qsf] set_vf_property failed: %d "
                                 "(continuing with sensor default)\n", rc);
        }

        // Private channel + connection for pulse delivery.
        chid_ = ChannelCreate(_NTO_CHF_PRIVATE);
        if (chid_ == -1) {
            std::fprintf(stderr, "[camera:qsf] ChannelCreate failed\n");
            return false;
        }
        coid_ = ConnectAttach(0, 0, chid_, _NTO_SIDE_CHANNEL, 0);
        if (coid_ == -1) {
            std::fprintf(stderr, "[camera:qsf] ConnectAttach failed\n");
            return false;
        }

        // Ask the camera to pulse us when a viewfinder buffer is ready.
        struct sigevent ev;
        SIGEV_PULSE_PTR_INIT(&ev, coid_, SIGEV_PULSE_PRIO_INHERIT,
                             kPulseBufferAvailable, this);
        rc = camera_enable_viewfinder_event(handle_, CAMERA_EVENTMODE_READONLY,
                                            &buffer_key_, &ev);
        if (rc != CAMERA_EOK) {
            std::fprintf(stderr, "[camera:qsf] enable_viewfinder_event: %d\n",
                         rc);
            return false;
        }

        rc = camera_start_viewfinder(handle_, NULL, NULL, NULL);
        if (rc != CAMERA_EOK) {
            std::fprintf(stderr, "[camera:qsf] start_viewfinder failed: %d\n",
                         rc);
            return false;
        }

        running_.store(true);
        thread_ = std::thread(&QnxCameraSource::acquisitionLoop, this);
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
        if (chid_ != -1 && running_.load()) {
            // Wake the acquisition thread so it can exit.
            running_.store(false);
            MsgSendPulse(coid_, -1, kPulseStop, 0);
        }
        if (thread_.joinable()) thread_.join();
        cv_.notify_all();

        if (handle_ != CAMERA_HANDLE_INVALID) {
            camera_stop_viewfinder(handle_);
            camera_disable_event(handle_, buffer_key_);
        }
        if (coid_ != -1) { ConnectDetach(coid_); coid_ = -1; }
        if (chid_ != -1) { ChannelDestroy(chid_); chid_ = -1; }
        if (handle_ != CAMERA_HANDLE_INVALID) {
            camera_close(handle_);
            handle_ = CAMERA_HANDLE_INVALID;
        }
    }

private:
    void acquisitionLoop() {
        struct _pulse pulse;
        while (running_.load()) {
            if (MsgReceivePulse(chid_, &pulse, sizeof(pulse), NULL) != 0) {
                break;
            }
            if (pulse.code == kPulseStop) break;
            if (pulse.code != kPulseBufferAvailable) continue;

            camera_buffer_t cbuf;
            if (camera_get_viewfinder_buffers(handle_, buffer_key_, &cbuf,
                                              NULL) != CAMERA_EOK) {
                continue;
            }

            cv::Mat bgr;
            const bool ok = bufferToBgr(cbuf, bgr);
            camera_return_buffer(handle_, &cbuf);  // return ASAP

            if (ok) {
                std::lock_guard<std::mutex> lock(mtx_);
                latest_ = std::move(bgr);
                latest_ts_ = nowNs();
                latest_id_ = static_cast<uint64_t>(cbuf.frametimestamp);
                if (latest_id_ <= delivered_id_) latest_id_ = delivered_id_ + 1;
                cv_.notify_one();
            }
        }
    }

    const Config& cfg_;
    camera_handle_t handle_ = CAMERA_HANDLE_INVALID;
    camera_eventkey_t buffer_key_ = 0;
    int chid_ = -1;
    int coid_ = -1;

    std::thread thread_;
    std::atomic<bool> running_{false};
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
                 "[camera] backend '%s' needs a QNX build "
                 "(-DLIFEGUARD_QNX_CAMERA=ON). Use camera_backend=file "
                 "for host testing.\n",
                 cfg.camera_backend.c_str());
    return std::make_unique<FileCameraSource>(cfg);
#endif
}

}  // namespace lifeguard
