#include "lifeguard/pose_estimator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include <opencv2/imgproc.hpp>

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"

namespace lifeguard {

struct PoseEstimator::Impl {
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter> interpreter;
    TfLiteDelegate* xnnpack = nullptr;
    int input_w = 192;   // MoveNet Lightning default
    int input_h = 192;
    bool input_uint8 = true;

    ~Impl() {
        if (xnnpack) TfLiteXNNPackDelegateDelete(xnnpack);
    }
};

PoseEstimator::PoseEstimator(Options opts)
    : impl_(std::make_unique<Impl>()), opts_(std::move(opts)) {}

PoseEstimator::~PoseEstimator() = default;

bool PoseEstimator::init() {
    impl_->model =
        tflite::FlatBufferModel::BuildFromFile(opts_.model_path.c_str());
    if (!impl_->model) {
        std::fprintf(stderr, "[pose] failed to load model: %s\n",
                     opts_.model_path.c_str());
        return false;
    }

    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*impl_->model, resolver);
    if (builder(&impl_->interpreter) != kTfLiteOk || !impl_->interpreter) {
        std::fprintf(stderr, "[pose] failed to build interpreter\n");
        return false;
    }

    TfLiteXNNPackDelegateOptions xopts = TfLiteXNNPackDelegateOptionsDefault();
    xopts.num_threads = opts_.num_threads;
    impl_->xnnpack = TfLiteXNNPackDelegateCreate(&xopts);
    if (impl_->xnnpack) {
        impl_->interpreter->ModifyGraphWithDelegate(impl_->xnnpack);
    }

    if (impl_->interpreter->AllocateTensors() != kTfLiteOk) {
        std::fprintf(stderr, "[pose] AllocateTensors failed\n");
        return false;
    }

    const int in_idx = impl_->interpreter->inputs()[0];
    const TfLiteTensor* in = impl_->interpreter->tensor(in_idx);
    if (in->dims->size == 4) {
        impl_->input_h = in->dims->data[1];
        impl_->input_w = in->dims->data[2];
    }
    impl_->input_uint8 = (in->type == kTfLiteUInt8);
    return true;
}

Pose PoseEstimator::estimate(const Frame& frame, const cv::Rect& roi) {
    Pose pose;
    if (!impl_->interpreter || frame.empty()) return pose;

    // Clamp ROI to the frame and crop the swimmer.
    const cv::Rect safe = roi & cv::Rect(0, 0, frame.image.cols,
                                         frame.image.rows);
    if (safe.width <= 0 || safe.height <= 0) return pose;
    const cv::Mat crop = frame.image(safe);

    // MoveNet expects a square input; letterbox the crop.
    const int side = std::max(safe.width, safe.height);
    cv::Mat square(side, side, crop.type(), cv::Scalar(0, 0, 0));
    const int off_x = (side - safe.width) / 2;
    const int off_y = (side - safe.height) / 2;
    crop.copyTo(square(cv::Rect(off_x, off_y, safe.width, safe.height)));

    cv::Mat input;
    cv::resize(square, input, cv::Size(impl_->input_w, impl_->input_h));
    cv::cvtColor(input, input, cv::COLOR_BGR2RGB);

    const int in_idx = impl_->interpreter->inputs()[0];
    TfLiteTensor* in = impl_->interpreter->tensor(in_idx);
    if (impl_->input_uint8) {
        std::memcpy(in->data.raw, input.data, input.total() * input.elemSize());
    } else {
        cv::Mat f;
        input.convertTo(f, CV_32FC3, 1.0 / 255.0);
        std::memcpy(in->data.raw, f.data, f.total() * f.elemSize());
    }

    if (impl_->interpreter->Invoke() != kTfLiteOk) {
        std::fprintf(stderr, "[pose] Invoke failed\n");
        return pose;
    }

    // Output: [1, 1, 17, 3] as (y, x, score), normalized to the square input.
    const float* kp = impl_->interpreter->typed_output_tensor<float>(0);
    if (!kp) return pose;

    const int count = static_cast<int>(Keypoint::kCount);
    for (int i = 0; i < count; ++i) {
        const float ny = kp[i * 3 + 0];
        const float nx = kp[i * 3 + 1];
        const float sc = kp[i * 3 + 2];

        // Undo the square letterbox back into full-frame coordinates.
        const float sx = nx * side - off_x + safe.x;
        const float sy = ny * side - off_y + safe.y;
        pose.points[i] = {sx, sy};
        pose.scores[i] = sc;
    }
    return pose;
}

}  // namespace lifeguard
