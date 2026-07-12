// Camera-only capture and MJPEG sender. This intentionally has no TensorFlow
// dependency: on QNX it captures IMX708 frames through QSF and sends them to
// the Mac, where ai_lifeguard performs all inference and annotation.

#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <opencv2/imgcodecs.hpp>
#ifdef LIFEGUARD_CAMERA_TEST_PREVIEW
#include <opencv2/highgui.hpp>
#endif

#include "lifeguard/camera_source.hpp"
#include "lifeguard/config.hpp"
#include "lifeguard/frame.hpp"
#include "lifeguard/mjpeg_server.hpp"

namespace {
std::atomic<bool> running{true};
void stop(int) { running.store(false); }
}

int main(int argc, char** argv) {
    using namespace lifeguard;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif

    std::string config_path = "config/lifeguard-qnx-camera.conf";
    std::string snapshot = "camera_test.jpg";
    int max_frames = 0;
    int stream_port = 8090;
    int stream_width = 1280;
    int jpeg_quality = 80;
    bool save_snapshot = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
            snapshot = argv[++i];
        } else if (std::strcmp(argv[i], "--stream-port") == 0 && i + 1 < argc) {
            stream_port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--stream-width") == 0 && i + 1 < argc) {
            stream_width = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--jpeg-quality") == 0 && i + 1 < argc) {
            jpeg_quality = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-snapshot") == 0) {
            save_snapshot = false;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::fprintf(stderr,
                "usage: camera_test [--config FILE] [--frames N] "
                "[--stream-port 8090] [--stream-width 1280] "
                "[--jpeg-quality 80] [--snapshot FILE|--no-snapshot]\n");
            return 0;
        }
    }

    Config cfg;
    if (!Config::load(config_path, cfg)) {
        std::fprintf(stderr, "[camtest] cannot read config: %s\n",
                     config_path.c_str());
        return 1;
    }
    std::fprintf(stderr, "[camtest] backend=%s device=%s unit=%d\n",
                 cfg.camera_backend.c_str(), cfg.camera_device.c_str(),
                 cfg.camera_unit);

    auto camera = CameraSource::create(cfg);
    if (!camera->open()) {
        std::fprintf(stderr, "[camtest] camera open FAILED\n");
        return 1;
    }

    MjpegServer stream({stream_port, stream_width, jpeg_quality});
    if (stream_port > 0 && !stream.start()) {
        camera->close();
        return 1;
    }
    if (stream_port > 0) {
        std::fprintf(stderr,
                     "[camtest] live video URL: http://<pi-ip>:%d/video.mjpg\n",
                     stream_port);
    }

    Frame frame;
    int count = 0;
    bool saved = !save_snapshot;
    const auto started = std::chrono::steady_clock::now();
    const bool file_input = cfg.camera_backend == "file";
    const auto frame_period = std::chrono::duration_cast<
        std::chrono::steady_clock::duration>(std::chrono::duration<double>(
            1.0 / std::max(1, cfg.target_fps)));
    auto next_publish = started;
#ifdef LIFEGUARD_CAMERA_TEST_PREVIEW
    cv::namedWindow("AI Lifeguard Camera", cv::WINDOW_NORMAL);
#endif

    while (running.load() && (max_frames <= 0 || count < max_frames)) {
        if (!camera->read(frame) || frame.image.empty()) break;
        if (file_input) {
            std::this_thread::sleep_until(
                started + frame_period * static_cast<double>(count));
        }
        if (!saved) {
            saved = cv::imwrite(snapshot, frame.image);
            if (saved) {
                std::fprintf(stderr, "[camtest] first frame %dx%d saved to %s\n",
                             frame.image.cols, frame.image.rows, snapshot.c_str());
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (stream_port > 0 && (file_input || now >= next_publish)) {
            stream.publish(frame.image);
            next_publish = now + frame_period;
        }
        ++count;
        if (count % 300 == 0) {
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            std::fprintf(stderr, "[camtest] streaming %.1f fps (%d frames)\n",
                         count / std::max(0.001, seconds), count);
        }
#ifdef LIFEGUARD_CAMERA_TEST_PREVIEW
        cv::imshow("AI Lifeguard Camera", frame.image);
        const int key = cv::waitKey(1);
        if (key == 'q' || key == 'Q' || key == 27) break;
#endif
    }

    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    stream.stop();
    camera->close();
#ifdef LIFEGUARD_CAMERA_TEST_PREVIEW
    cv::destroyAllWindows();
#endif
    std::fprintf(stderr, "[camtest] captured %d frames in %.2fs (%.1f fps)\n",
                 count, seconds, count / std::max(0.001, seconds));
    return count > 0 ? 0 : 1;
}
