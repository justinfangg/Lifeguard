#pragma once

#include <string>
#include <unordered_map>

#include "lifeguard/pose_estimator.hpp"
#include "lifeguard/tracker.hpp"

namespace lifeguard {

// Result of evaluating one swimmer at one point in time.
struct DistressAssessment {
    int track_id = -1;
    float score = 0.0f;   // instantaneous distress score [0,1]
    float smoothed_score = 0.0f;
    bool potential = false;
    bool alerting = false; // condition has persisted long enough to alert
    std::string reason;    // human-readable explanation for logs/UI
};

// Turns per-frame pose + track motion into a sustained distress decision.
//
// The instantaneous score combines interpretable features of the Instinctive
// Drowning Response:
//   * vertical torso orientation,
//   * head low relative to shoulders (mouth near/under water line),
//   * low horizontal velocity (no forward progress),
//   * vertical bobbing.
//
// An alert is only raised once the score stays above threshold for
// `persist_seconds`, per track (debouncing).
class DistressAnalyzer {
public:
    struct Options {
        float score_threshold = 0.6f;   // instantaneous distress cutoff
        float persist_seconds = 4.0f;   // must hold this long to alert
        float window_seconds = 6.0f;    // motion feature window
        float potential_threshold = 0.2f;
        float potential_hold_seconds = 2.0f;
    };

    DistressAnalyzer() = default;
    explicit DistressAnalyzer(const Options& opts) : opts_(opts) {}

    // Evaluate one tracked swimmer given its latest pose.
    DistressAssessment evaluate(const Track& track,
                                const Pose& pose,
                                uint64_t timestamp_ns);

    // Forget state for tracks that no longer exist.
    void gc(const std::vector<Track>& live_tracks);

private:
    struct State {
        uint64_t distress_since_ns = 0;  // 0 == not currently distressed
        uint64_t potential_until_ns = 0;
        float last_score = 0.0f;
        float smoothed_score = 0.0f;
    };

    float computeInstantScore(const Track& track, const Pose& pose,
                              std::string& reason) const;

    Options opts_;
    std::unordered_map<int, State> state_;
};

}  // namespace lifeguard
