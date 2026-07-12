#include "lifeguard/config.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace lifeguard {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool toBool(const std::string& v) {
    std::string lower = v;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

}  // namespace

bool Config::load(const std::string& path, Config& out) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if (key == "video_file") out.video_file = val;
        else if (key == "camera_device") out.camera_device = val;
        else if (key == "camera_backend") out.camera_backend = val;
        else if (key == "camera_unit") out.camera_unit = std::stoi(val);
        else if (key == "frame_width") out.frame_width = std::stoi(val);
        else if (key == "frame_height") out.frame_height = std::stoi(val);
        else if (key == "target_fps") out.target_fps = std::stoi(val);
        else if (key == "display") out.display = toBool(val);
        else if (key == "output_video") out.output_video = val;
        else if (key == "stream_enabled") out.stream_enabled = toBool(val);
        else if (key == "stream_port") out.stream_port = std::stoi(val);
        else if (key == "stream_width") out.stream_width = std::stoi(val);
        else if (key == "stream_jpeg_quality")
            out.stream_jpeg_quality = std::stoi(val);
        else if (key == "detector_model") out.detector_model = val;
        else if (key == "pose_model") out.pose_model = val;
        else if (key == "num_threads") out.num_threads = std::stoi(val);
        else if (key == "detector_score_threshold")
            out.detector_score_threshold = std::stof(val);
        else if (key == "person_class_id")
            out.person_class_id = std::stoi(val);
        else if (key == "distress_persist_seconds")
            out.distress_persist_seconds = std::stof(val);
        else if (key == "temporal_window_seconds")
            out.temporal_window_seconds = std::stof(val);
        else if (key == "distress_score_threshold")
            out.distress_score_threshold = std::stof(val);
        else if (key == "potential_distress_score_threshold")
            out.potential_distress_score_threshold = std::stof(val);
        else if (key == "potential_enter_seconds")
            out.potential_enter_seconds = std::stof(val);
        else if (key == "potential_clear_seconds")
            out.potential_clear_seconds = std::stof(val);
        else if (key == "alert_clear_seconds")
            out.alert_clear_seconds = std::stof(val);
        else if (key == "alert_log") out.alert_log = toBool(val);
        else if (key == "log_path") out.log_path = val;
        // Unknown keys are ignored so newer configs stay backward compatible.
    }
    return true;
}

}  // namespace lifeguard
