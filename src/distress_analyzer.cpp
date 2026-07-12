#include "lifeguard/distress_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lifeguard {

namespace {

constexpr float kNsPerSec = 1e9f;

// Distance between two keypoints, or -1 if either is low-confidence.
float dist(const Pose& p, Keypoint a, Keypoint b, float min_score) {
    if (p.score(a) < min_score || p.score(b) < min_score) return -1.0f;
    return static_cast<float>(cv::norm(p.at(a) - p.at(b)));
}

cv::Point2f midpoint(const cv::Point2f& a, const cv::Point2f& b) {
    return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
}

}  // namespace

float DistressAnalyzer::computeInstantScore(const Track& track,
                                            const Pose& pose,
                                            std::string& reason) const {
    // Water, glare, and partial bodies lower pose confidence substantially.
    // Keep this permissive for the MVP and combine several temporal signals
    // before escalating from potential danger to a sustained alert.
    constexpr float kMinKp = 0.15f;
    float score = 0.0f;
    reason.clear();

    const bool have_shoulders =
        pose.score(Keypoint::kLeftShoulder) >= kMinKp &&
        pose.score(Keypoint::kRightShoulder) >= kMinKp;
    const bool have_hips = pose.score(Keypoint::kLeftHip) >= kMinKp &&
                           pose.score(Keypoint::kRightHip) >= kMinKp;

    // --- Feature 1: vertical torso orientation -------------------------
    // A swimming person is roughly horizontal; a drowning person is upright.
    if (have_shoulders && have_hips) {
        const cv::Point2f shoulder =
            midpoint(pose.at(Keypoint::kLeftShoulder),
                     pose.at(Keypoint::kRightShoulder));
        const cv::Point2f hip = midpoint(pose.at(Keypoint::kLeftHip),
                                         pose.at(Keypoint::kRightHip));
        const cv::Point2f torso = hip - shoulder;
        const float vertical =
            std::abs(torso.y) /
            (std::abs(torso.x) + std::abs(torso.y) + 1e-3f);
        if (vertical > 0.7f) {
            score += 0.35f;
            reason += "vertical-torso ";
        }
    }

    // --- Feature 2: head low relative to shoulders ---------------------
    // Mouth at/under the water line: nose sinks toward/under the shoulders.
    if (have_shoulders && pose.score(Keypoint::kNose) >= kMinKp) {
        const float shoulder_y =
            midpoint(pose.at(Keypoint::kLeftShoulder),
                     pose.at(Keypoint::kRightShoulder))
                .y;
        const float nose_y = pose.at(Keypoint::kNose).y;
        const float shoulder_w =
            dist(pose, Keypoint::kLeftShoulder, Keypoint::kRightShoulder, kMinKp);
        if (shoulder_w > 0 && (nose_y > shoulder_y - 0.2f * shoulder_w)) {
            score += 0.25f;
            reason += "head-low ";
        }
    }

    // --- Feature 3: low horizontal progress ----------------------------
    // Compare displacement over the motion window against body size.
    if (track.centroids.size() >= 2) {
        const auto& first = track.centroids.front();
        const auto& last = track.centroids.back();
        const float dt =
            (last.first - first.first) / kNsPerSec;  // seconds
        if (dt >= opts_.window_seconds * 0.5f) {
            const float horiz =
                std::abs(last.second.x - first.second.x);
            const float body = std::max(track.box.width, 1.0f);
            if (horiz < 0.5f * body) {
                score += 0.20f;
                reason += "no-progress ";
            }
        }
    }

    // --- Feature 4: vertical bobbing -----------------------------------
    // Detect meaningful vertical travel. Direction changes strengthen the
    // signal, but one sharp rise/fall is still shown as potential danger so
    // the operator sees the red overlay early.
    if (track.centroids.size() >= 4) {
        float min_y = std::numeric_limits<float>::max();
        float max_y = std::numeric_limits<float>::lowest();
        int reversals = 0;
        int previous_direction = 0;
        const float noise = std::max(2.0f, track.box.height * 0.025f);
        for (const auto& c : track.centroids) {
            min_y = std::min(min_y, c.second.y);
            max_y = std::max(max_y, c.second.y);
        }
        for (size_t i = 1; i < track.centroids.size(); ++i) {
            const float dy = track.centroids[i].second.y -
                             track.centroids[i - 1].second.y;
            if (std::abs(dy) < noise) continue;
            const int direction = dy > 0.0f ? 1 : -1;
            if (previous_direction != 0 && direction != previous_direction) {
                ++reversals;
            }
            previous_direction = direction;
        }
        const float body = std::max(track.box.height, 1.0f);
        const float travel = (max_y - min_y) / body;
        if (travel > 0.18f) {
            score += reversals >= 2 ? 0.35f : 0.25f;
            reason += reversals >= 2 ? "repeated-bobbing " : "vertical-motion ";
        }
    }

    return std::min(score, 1.0f);
}

DistressAssessment DistressAnalyzer::evaluate(const Track& track,
                                              const Pose& pose,
                                              uint64_t timestamp_ns) {
    DistressAssessment result;
    result.track_id = track.id;
    result.score = computeInstantScore(track, pose, result.reason);

    State& st = state_[track.id];
    st.last_score = result.score;

    // Light temporal smoothing keeps the UI from flashing green between
    // adjacent danger frames. A potential condition is latched briefly so a
    // lifeguard has time to see it while the stricter alert timer continues.
    st.smoothed_score = st.smoothed_score == 0.0f
                            ? result.score
                            : 0.65f * st.smoothed_score + 0.35f * result.score;
    result.smoothed_score = st.smoothed_score;
    if (result.score >= opts_.potential_threshold ||
        st.smoothed_score >= opts_.potential_threshold) {
        st.potential_until_ns = timestamp_ns + static_cast<uint64_t>(
            std::max(0.0f, opts_.potential_hold_seconds) * kNsPerSec);
    }
    result.potential = timestamp_ns <= st.potential_until_ns;

    if (std::max(result.score, st.smoothed_score) >= opts_.score_threshold) {
        if (st.distress_since_ns == 0) {
            st.distress_since_ns = timestamp_ns;
        }
        const float held =
            (timestamp_ns - st.distress_since_ns) / kNsPerSec;
        result.alerting = held >= opts_.persist_seconds;
    } else {
        st.distress_since_ns = 0;  // reset the timer
        result.alerting = false;
    }

    return result;
}

void DistressAnalyzer::gc(const std::vector<Track>& live_tracks) {
    for (auto it = state_.begin(); it != state_.end();) {
        const bool alive =
            std::any_of(live_tracks.begin(), live_tracks.end(),
                        [&](const Track& t) { return t.id == it->first; });
        it = alive ? std::next(it) : state_.erase(it);
    }
}

}  // namespace lifeguard
