#pragma once

#include <string>

namespace lifeguard {

// Runtime configuration, loaded from a simple `key = value` .conf file.
struct Config {
    // --- Video input ---------------------------------------------------
    std::string video_file;              // recorded footage to analyze (MP4)
    // OpenCV camera index, device path, or network MJPEG URL.
    std::string camera_device = "http://127.0.0.1:8090/video.mjpg";
    std::string camera_backend = "network";  // file, uvc/network, or QNX csi
    int camera_unit = 1;                 // QSF unit used by the Pi sender
    int frame_width = 1280;
    int frame_height = 720;
    int target_fps = 15;
    bool display = true;
    std::string output_video = "videos/lifeguard_annotated.mp4";
    bool stream_enabled = false;
    int stream_port = 8080;
    int stream_width = 960;
    int stream_jpeg_quality = 80;

    // --- Models --------------------------------------------------------
    std::string detector_model = "models/swimmer_detector_int8.tflite";
    std::string pose_model = "models/movenet_lightning_int8.tflite";
    int num_threads = 4;                 // TFLite / XNNPACK worker threads
    float detector_score_threshold = 0.5f;
    // Class index the detector treats as "person/swimmer". For the COCO
    // This project's COCO SSD-MobileNet export uses 0 for person.
    int person_class_id = 0;

    // --- Distress logic ------------------------------------------------
    // How long the distress condition must persist before alerting (seconds).
    float distress_persist_seconds = 4.0f;
    // Sliding window length used by the temporal analyzer (seconds).
    float temporal_window_seconds = 6.0f;
    float distress_score_threshold = 0.6f;
    float potential_distress_score_threshold = 0.2f;
    float potential_hold_seconds = 2.0f;

    // --- Alerting ------------------------------------------------------
    bool alert_log = true;
    std::string log_path = "lifeguard.log";

    // Load configuration from `path`. Missing keys keep their defaults.
    // Returns false if the file cannot be opened.
    static bool load(const std::string& path, Config& out);
};

}  // namespace lifeguard
