#pragma once

#include <array>
#include <memory>
#include <string>

#include <opencv2/core.hpp>

#include "lifeguard/frame.hpp"

namespace lifeguard {

// MoveNet 17-keypoint (COCO) ordering.
enum class Keypoint : int {
    kNose = 0,
    kLeftEye, kRightEye, kLeftEar, kRightEar,
    kLeftShoulder, kRightShoulder,
    kLeftElbow, kRightElbow,
    kLeftWrist, kRightWrist,
    kLeftHip, kRightHip,
    kLeftKnee, kRightKnee,
    kLeftAnkle, kRightAnkle,
    kCount  // == 17
};

struct Pose {
    // Keypoints in source-frame pixel coordinates + per-point confidence.
    std::array<cv::Point2f, static_cast<int>(Keypoint::kCount)> points{};
    std::array<float, static_cast<int>(Keypoint::kCount)> scores{};

    const cv::Point2f& at(Keypoint k) const {
        return points[static_cast<int>(k)];
    }
    float score(Keypoint k) const { return scores[static_cast<int>(k)]; }
};

// TFLite single-person pose estimator (MoveNet Lightning).
// Run once per tracked swimmer on the cropped bounding box.
class PoseEstimator {
public:
    struct Options {
        std::string model_path;
        int num_threads = 2;
        float min_keypoint_score = 0.3f;
    };

    explicit PoseEstimator(Options opts);
    ~PoseEstimator();

    bool init();

    // Estimate pose inside `roi` of `frame`. Points are returned in full
    // frame coordinates.
    Pose estimate(const Frame& frame, const cv::Rect& roi);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Options opts_;
};

}  // namespace lifeguard
