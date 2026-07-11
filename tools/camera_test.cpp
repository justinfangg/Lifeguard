// Standalone camera smoke-test.
//
// Opens the camera described by the config (same CameraSource used by the app),
// grabs a few frames, saves the first one to a JPEG and reports the measured
// frame rate. It links ONLY OpenCV — no TensorFlow Lite or models required —
// so you can verify a physical webcam works before building the full pipeline.
//
// Host (USB webcam):
//   set camera_backend = uvc and camera_device = 0 in the config, then:
//     ./camera_test --config config/lifeguard.conf
//
// QNX target: build with -DLIFEGUARD_QNX_CAMERA=ON to exercise the QSF path.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <opencv2/imgcodecs.hpp>

#include "lifeguard/camera_source.hpp"
#include "lifeguard/config.hpp"
#include "lifeguard/frame.hpp"

int main(int argc, char** argv) {
    using namespace lifeguard;

    std::string config_path = "config/lifeguard.conf";
    std::string snapshot = "camera_test.jpg";
    int max_frames = 100;

    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0) {
            max_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--snapshot") == 0) {
            snapshot = argv[++i];
        }
    }

    Config cfg;
    if (!Config::load(config_path, cfg)) {
        std::fprintf(stderr, "[camtest] no config at '%s'; using defaults\n",
                     config_path.c_str());
    }
    std::fprintf(stderr, "[camtest] backend=%s device=%s unit=%d\n",
                 cfg.camera_backend.c_str(), cfg.camera_device.c_str(),
                 cfg.camera_unit);

    auto cam = CameraSource::create(cfg);
    if (!cam->open()) {
        std::fprintf(stderr, "[camtest] camera open FAILED\n");
        return 1;
    }

    Frame frame;
    int count = 0;
    bool saved = false;
    const auto t0 = std::chrono::steady_clock::now();

    while (count < max_frames) {
        if (!cam->read(frame) || frame.image.empty()) {
            std::fprintf(stderr, "[camtest] read failed at frame %d\n", count);
            break;
        }
        if (!saved) {
            if (cv::imwrite(snapshot, frame.image)) {
                std::fprintf(stderr,
                             "[camtest] first frame %dx%d saved to %s\n",
                             frame.image.cols, frame.image.rows,
                             snapshot.c_str());
            } else {
                std::fprintf(stderr, "[camtest] could not write %s\n",
                             snapshot.c_str());
            }
            saved = true;
        }
        ++count;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    cam->close();

    if (count == 0) {
        std::fprintf(stderr, "[camtest] no frames captured\n");
        return 1;
    }
    std::fprintf(stderr, "[camtest] captured %d frames in %.2fs (%.1f fps)\n",
                 count, secs, count / (secs > 0.0 ? secs : 1.0));
    return 0;
}
