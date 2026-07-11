#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "lifeguard/alerter.hpp"
#include "lifeguard/camera_source.hpp"
#include "lifeguard/config.hpp"
#include "lifeguard/detector.hpp"
#include "lifeguard/distress_analyzer.hpp"
#include "lifeguard/frame.hpp"
#include "lifeguard/pose_estimator.hpp"
#include "lifeguard/ring_buffer.hpp"
#include "lifeguard/tracker.hpp"

namespace {

std::atomic<bool> g_running{true};

void onSignal(int) { g_running.store(false); }

std::string argValue(int argc, char** argv, const char* flag,
                     const std::string& fallback) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
    }
    return fallback;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace lifeguard;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const std::string config_path =
        argValue(argc, argv, "--config", "config/lifeguard.conf");

    Config cfg;
    if (!Config::load(config_path, cfg)) {
        std::fprintf(stderr,
                     "[main] could not read config '%s'; using defaults\n",
                     config_path.c_str());
    }

    // --- Build the pipeline components ---------------------------------
    auto camera = CameraSource::create(cfg);
    if (!camera->open()) {
        std::fprintf(stderr, "[main] camera open failed\n");
        return 1;
    }

    Detector::Options dopts;
    dopts.model_path = cfg.detector_model;
    dopts.num_threads = cfg.num_threads;
    dopts.score_threshold = cfg.detector_score_threshold;
    dopts.person_class_id = cfg.person_class_id;
    Detector detector(dopts);
    if (!detector.init()) {
        std::fprintf(stderr, "[main] detector init failed\n");
        return 1;
    }

    PoseEstimator pose({cfg.pose_model, std::max(1, cfg.num_threads / 2)});
    const bool pose_ok = pose.init();
    if (!pose_ok) {
        std::fprintf(stderr,
                     "[main] pose model unavailable; running detection + "
                     "motion-based distress features only\n");
    }

    Tracker tracker;
    DistressAnalyzer analyzer({0.6f, cfg.distress_persist_seconds,
                               cfg.temporal_window_seconds});
    Alerter alerter(cfg);
    alerter.init();

    // Shared frame queue between capture and inference threads.
    RingBuffer<Frame, 4> queue;

    // --- Capture thread ------------------------------------------------
    std::thread capture_thread([&] {
        Frame f;
        while (g_running.load()) {
            if (!camera->read(f)) {
                std::fprintf(stderr, "[capture] end of stream / read error\n");
                g_running.store(false);
                break;
            }
            queue.push(f);  // drop-oldest if the consumer is behind
        }
    });

    // --- Inference loop (main thread) ----------------------------------
    while (g_running.load()) {
        auto maybe = queue.pop();
        if (!maybe) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        Frame frame = std::move(*maybe);

        const std::vector<Detection> dets = detector.detect(frame);
        const std::vector<Track>& tracks =
            tracker.update(dets, frame.timestamp_ns);

        for (const Track& t : tracks) {
            if (t.misses > 0) continue;  // only assess freshly-seen swimmers
            const cv::Rect roi(
                static_cast<int>(t.box.x), static_cast<int>(t.box.y),
                static_cast<int>(t.box.width),
                static_cast<int>(t.box.height));
            const Pose p = pose_ok ? pose.estimate(frame, roi) : Pose{};
            const DistressAssessment a =
                analyzer.evaluate(t, p, frame.timestamp_ns);
            alerter.handle(a, frame.timestamp_ns);
        }
        analyzer.gc(tracks);
    }

    capture_thread.join();
    camera->close();
    std::fprintf(stderr, "[main] shut down cleanly\n");
    return 0;
}
