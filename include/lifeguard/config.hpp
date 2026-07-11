#pragma once

#include <string>

namespace lifeguard {

// Runtime configuration, loaded from a simple `key = value` .conf file.
struct Config {
    // --- Camera --------------------------------------------------------
    std::string camera_backend = "uvc";  // "uvc" | "csi" | "file"
    std::string camera_device = "/dev/video0";
    std::string video_file;              // used when camera_backend == "file"
    int frame_width = 1280;
    int frame_height = 720;
    int target_fps = 15;

    // --- Models --------------------------------------------------------
    std::string detector_model = "models/swimmer_detector_int8.tflite";
    std::string pose_model = "models/movenet_lightning_int8.tflite";
    int num_threads = 4;                 // TFLite / XNNPACK worker threads
    float detector_score_threshold = 0.5f;

    // --- Distress logic ------------------------------------------------
    // How long the distress condition must persist before alerting (seconds).
    float distress_persist_seconds = 4.0f;
    // Sliding window length used by the temporal analyzer (seconds).
    float temporal_window_seconds = 6.0f;

    // --- Alerting ------------------------------------------------------
    bool alert_gpio = false;
    int alert_gpio_pin = 17;
    bool alert_log = true;
    std::string log_path = "/data/lifeguard/lifeguard.log";

    // Load configuration from `path`. Missing keys keep their defaults.
    // Returns false if the file cannot be opened.
    static bool load(const std::string& path, Config& out);
};

}  // namespace lifeguard
