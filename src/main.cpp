#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#if !defined(__QNXNTO__)
#include <opencv2/highgui.hpp>
#endif

#include "lifeguard/alerter.hpp"
#include "lifeguard/camera_source.hpp"
#include "lifeguard/config.hpp"
#include "lifeguard/detector.hpp"
#include "lifeguard/distress_analyzer.hpp"
#include "lifeguard/frame.hpp"
#include "lifeguard/mjpeg_server.hpp"
#include "lifeguard/pose_estimator.hpp"
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

    if (cfg.camera_backend == "file" &&
        (cfg.video_file.empty() || !std::filesystem::exists(cfg.video_file))) {
        std::fprintf(stderr, "[main] video file not found: %s\n",
                     cfg.video_file.c_str());
        return 1;
    }
    if (cfg.detector_model.empty() ||
        !std::filesystem::exists(cfg.detector_model)) {
        std::fprintf(stderr, "[main] detector model not found: %s\n",
                     cfg.detector_model.c_str());
        return 1;
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

    if (cfg.pose_model.empty() || !std::filesystem::exists(cfg.pose_model)) {
        std::fprintf(stderr,
                     "[main] MoveNet pose model not found: %s\n"
                     "[main] download MoveNet Lightning and place it at that path\n",
                     cfg.pose_model.c_str());
        return 1;
    }

    PoseEstimator pose({cfg.pose_model, std::max(1, cfg.num_threads / 2)});
    if (!pose.init()) {
        std::fprintf(stderr, "[main] MoveNet pose initialization failed\n");
        return 1;
    }
    std::fprintf(stderr, "[main] detector and MoveNet models initialized\n");

    Tracker tracker;
    DistressAnalyzer analyzer({cfg.distress_score_threshold,
                               cfg.distress_persist_seconds,
                               cfg.temporal_window_seconds});
    Alerter alerter(cfg);
    alerter.init();

    MjpegServer stream({cfg.stream_port, cfg.stream_width,
                        cfg.stream_jpeg_quality});
    if (cfg.stream_enabled && !stream.start()) {
        std::fprintf(stderr, "[main] annotated preview is unavailable\n");
    }

    // Pace recorded clips for preview; live camera capture already blocks
    // until OpenCV supplies a new frame.
    const bool file_input = cfg.camera_backend == "file";
    const auto playback_start = std::chrono::steady_clock::now();
    const auto frame_period = std::chrono::duration<double>(
        1.0 / std::max(1, cfg.target_fps));
    cv::VideoWriter output;
    bool output_initialized = false;

    // --- Inference and display loop ------------------------------------
    Frame frame;
    while (g_running.load() && camera->read(frame)) {
        if (file_input) {
            const auto target_time = playback_start +
                frame_period * static_cast<double>(frame.index);
            std::this_thread::sleep_until(target_time);
        }

        const std::vector<Detection> dets = detector.detect(frame);
        const std::vector<Track>& tracks =
            tracker.update(dets, frame.timestamp_ns);

        if (frame.index < 5 || frame.index % 30 == 0) {
            std::fprintf(stderr, "[pipeline] frame=%llu detections=%zu tracks=%zu\n",
                         static_cast<unsigned long long>(frame.index),
                         dets.size(), tracks.size());
        }

        cv::Mat display = frame.image.clone();

        if (!output_initialized && !cfg.output_video.empty()) {
            output.open(cfg.output_video,
                        cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                        static_cast<double>(std::max(1, cfg.target_fps)),
                        display.size(), true);
            output_initialized = true;
            if (!output.isOpened()) {
                std::fprintf(stderr, "[main] could not open output video: %s\n",
                             cfg.output_video.c_str());
            } else {
                std::fprintf(stderr, "[main] writing annotated video: %s\n",
                             cfg.output_video.c_str());
            }
        }

        for (const Track& t : tracks) {
            if (t.misses > 0) continue;  // only assess freshly-seen swimmers
            const cv::Rect roi(
                static_cast<int>(t.box.x), static_cast<int>(t.box.y),
                static_cast<int>(t.box.width),
                static_cast<int>(t.box.height));
            const Pose p = pose.estimate(frame, roi);
            const DistressAssessment a =
                analyzer.evaluate(t, p, frame.timestamp_ns);
            alerter.handle(a, frame.timestamp_ns);

            const bool potential = a.alerting ||
                                   a.score >= cfg.potential_distress_score_threshold;
            const cv::Scalar color = potential ? cv::Scalar(0, 0, 255)
                                               : cv::Scalar(0, 220, 0);
            const cv::Rect box = roi & cv::Rect(0, 0, display.cols, display.rows);
            cv::rectangle(display, box, color, 3);
            const std::string label = potential
                ? (a.alerting ? "DROWNING ALERT" : "POTENTIAL DANGER")
                : "SWIMMER";
            cv::putText(display, label, cv::Point(box.x, std::max(20, box.y - 8)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.65, color, 2,
                        cv::LINE_AA);
        }
        analyzer.gc(tracks);

        if (output.isOpened()) output.write(display);
        if (cfg.stream_enabled) stream.publish(display);

        #if !defined(__QNXNTO__)
        if (cfg.display) {
            cv::imshow("AI Lifeguard", display);
            const int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q') break;
        }
        #endif
    }

    if (g_running.load()) {
        std::fprintf(stderr, "[capture] end of stream / read error\n");
    }
    #if !defined(__QNXNTO__)
    if (cfg.display) cv::destroyAllWindows();
    #endif
    if (output.isOpened()) output.release();
    stream.stop();
    camera->close();
    std::fprintf(stderr, "[main] shut down cleanly\n");
    return 0;
}
