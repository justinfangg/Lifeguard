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
                                            std::string& reason,
                                            bool& horizontally_stationary) const {
    // Water, glare, and partial bodies lower pose confidence substantially.
    // Keep this permissive for the MVP and combine several temporal signals
    // before escalating from potential danger to a sustained alert.
    constexpr float kMinKp = 0.15f;
    float score = 0.0f;
    reason.clear();
    horizontally_stationary = false;

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
    // Compare the full horizontal span over the motion window against body
    // size. Start-to-end displacement alone would misclassify an out-and-back
    // swimmer as stationary.
    if (track.centroids.size() >= 2) {
        const auto& first = track.centroids.front();
        const auto& last = track.centroids.back();
        const float dt =
            (last.first - first.first) / kNsPerSec;  // seconds
        if (dt >= opts_.window_seconds * 0.5f) {
            float min_x = std::numeric_limits<float>::max();
            float max_x = std::numeric_limits<float>::lowest();
            for (const auto& c : track.centroids) {
                min_x = std::min(min_x, c.second.x);
                max_x = std::max(max_x, c.second.x);
            }
            const float horizontal_span = max_x - min_x;
            const float body = std::max(track.box.width, 1.0f);
            if (horizontal_span < 0.5f * body) {
                horizontally_stationary = true;
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
    result.score = computeInstantScore(track, pose, result.reason,
                                       result.horizontally_stationary);

    State& st = state_[track.id];
    st.last_score = result.score;

    // A drowning classification requires both the distress signs above and
    // low horizontal progress over the motion window. Do not let posture,
    // bobbing, a latched state, or a stale smoothed score classify a swimmer
    // who is actively travelling across the pool.
    if (!result.horizontally_stationary) {
        st.distress_since_ns = 0;
        st.potential_since_ns = 0;
        st.potential_recovery_since_ns = 0;
        st.alert_recovery_since_ns = 0;
        st.potential = false;
        st.alerting = false;
        st.smoothed_score = 0.0f;
        result.smoothed_score = result.score;
        return result;
    }

    // Smooth the score, then apply a state machine with hysteresis. Entering a
    // danger state requires sustained evidence. Leaving it requires a lower
    // score sustained for longer, preventing red/green flicker near a cutoff.
    st.smoothed_score = st.smoothed_score == 0.0f
                            ? result.score
                            : 0.65f * st.smoothed_score + 0.35f * result.score;
    result.smoothed_score = st.smoothed_score;
    const float decision_score = std::max(result.score, st.smoothed_score);
    const float clear_threshold = opts_.potential_threshold * 0.65f;

    if (!st.potential) {
        if (decision_score >= opts_.potential_threshold) {
            if (st.potential_since_ns == 0) st.potential_since_ns = timestamp_ns;
            const float held =
                (timestamp_ns - st.potential_since_ns) / kNsPerSec;
            if (held >= opts_.potential_enter_seconds) {
                st.potential = true;
                st.potential_recovery_since_ns = 0;
            }
        } else {
            st.potential_since_ns = 0;
        }
    } else if (decision_score < clear_threshold) {
        if (st.potential_recovery_since_ns == 0) {
            st.potential_recovery_since_ns = timestamp_ns;
        }
        const float safe_held =
            (timestamp_ns - st.potential_recovery_since_ns) / kNsPerSec;
        if (safe_held >= opts_.potential_clear_seconds) {
            st.potential = false;
            st.potential_since_ns = 0;
            st.potential_recovery_since_ns = 0;
        }
    } else {
        st.potential_recovery_since_ns = 0;
    }

    if (!st.alerting && decision_score >= opts_.score_threshold) {
        if (st.distress_since_ns == 0) st.distress_since_ns = timestamp_ns;
        const float held = (timestamp_ns - st.distress_since_ns) / kNsPerSec;
        if (held >= opts_.persist_seconds) {
            st.alerting = true;
            st.potential = true;
        }
    } else if (!st.alerting) {
        st.distress_since_ns = 0;
    }

    if (st.alerting) {
        if (decision_score < clear_threshold) {
            if (st.alert_recovery_since_ns == 0) {
                st.alert_recovery_since_ns = timestamp_ns;
            }
            const float safe_held =
                (timestamp_ns - st.alert_recovery_since_ns) / kNsPerSec;
            if (safe_held >= opts_.alert_clear_seconds) {
                st.alerting = false;
                st.distress_since_ns = 0;
                st.alert_recovery_since_ns = 0;
            }
        } else {
            st.alert_recovery_since_ns = 0;
        }
    }

    result.alerting = st.alerting;
    result.potential = st.potential || st.alerting;

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
