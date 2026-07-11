#pragma once

#include <deque>
#include <vector>

#include <opencv2/core.hpp>

#include "lifeguard/detector.hpp"

namespace lifeguard {

// A persistent identity for a swimmer across frames, with a short history of
// centroid positions used by the distress analyzer for motion features.
struct Track {
    int id = -1;
    cv::Rect2f box;                       // latest bounding box
    int age = 0;                          // frames since created
    int misses = 0;                       // consecutive frames without a match
    std::deque<std::pair<uint64_t, cv::Point2f>> centroids;  // (ts_ns, xy)

    cv::Point2f centroid() const {
        return {box.x + box.width * 0.5f, box.y + box.height * 0.5f};
    }
};

// Simple IOU-based multi-object tracker (SORT-lite, no Kalman filter for v1).
class Tracker {
public:
    struct Options {
        float iou_match_threshold = 0.3f;
        int max_misses = 15;        // drop a track after this many missed frames
        size_t max_history = 90;    // centroid samples to keep (~6s @ 15fps)
    };

    explicit Tracker(Options opts = {}) : opts_(opts) {}

    // Associate detections with existing tracks and return the live tracks.
    const std::vector<Track>& update(const std::vector<Detection>& detections,
                                     uint64_t timestamp_ns);

    const std::vector<Track>& tracks() const { return tracks_; }

private:
    Options opts_;
    std::vector<Track> tracks_;
    int next_id_ = 0;
};

}  // namespace lifeguard
