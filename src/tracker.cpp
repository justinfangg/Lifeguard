#include "lifeguard/tracker.hpp"

#include <algorithm>
#include <limits>

namespace lifeguard {

namespace {

float iou(const cv::Rect2f& a, const cv::Rect2f& b) {
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.width, b.x + b.width);
    const float y2 = std::min(a.y + a.height, b.y + b.height);
    const float w = std::max(0.0f, x2 - x1);
    const float h = std::max(0.0f, y2 - y1);
    const float inter = w * h;
    const float uni = a.width * a.height + b.width * b.height - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

}  // namespace

const std::vector<Track>& Tracker::update(
    const std::vector<Detection>& detections, uint64_t timestamp_ns) {
    const size_t n_tracks = tracks_.size();
    const size_t n_det = detections.size();

    std::vector<bool> det_used(n_det, false);
    std::vector<bool> track_matched(n_tracks, false);

    // Greedy IOU matching: repeatedly take the highest-IOU (track, det) pair.
    while (true) {
        float best_iou = opts_.iou_match_threshold;
        int best_t = -1, best_d = -1;
        for (size_t t = 0; t < n_tracks; ++t) {
            if (track_matched[t]) continue;
            for (size_t d = 0; d < n_det; ++d) {
                if (det_used[d]) continue;
                const float score = iou(tracks_[t].box, detections[d].box);
                if (score > best_iou) {
                    best_iou = score;
                    best_t = static_cast<int>(t);
                    best_d = static_cast<int>(d);
                }
            }
        }
        if (best_t < 0) break;

        Track& tr = tracks_[best_t];
        tr.box = detections[best_d].box;
        tr.misses = 0;
        tr.age += 1;
        tr.centroids.emplace_back(timestamp_ns, tr.centroid());
        while (tr.centroids.size() > opts_.max_history) tr.centroids.pop_front();

        track_matched[best_t] = true;
        det_used[best_d] = true;
    }

    // Unmatched existing tracks: age them out.
    for (size_t t = 0; t < n_tracks; ++t) {
        if (!track_matched[t]) {
            tracks_[t].misses += 1;
            tracks_[t].age += 1;
        }
    }

    // Unmatched detections become new tracks.
    for (size_t d = 0; d < n_det; ++d) {
        if (det_used[d]) continue;
        Track tr;
        tr.id = next_id_++;
        tr.box = detections[d].box;
        tr.age = 1;
        tr.misses = 0;
        tr.centroids.emplace_back(timestamp_ns, tr.centroid());
        tracks_.push_back(std::move(tr));
    }

    // Remove dead tracks.
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
                       [this](const Track& t) {
                           return t.misses > opts_.max_misses;
                       }),
        tracks_.end());

    return tracks_;
}

}  // namespace lifeguard
