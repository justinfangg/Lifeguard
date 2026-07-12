#include "lifeguard/detector.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include <opencv2/imgproc.hpp>

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#ifdef LIFEGUARD_USE_XNNPACK
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
#endif

#include "lifeguard/preprocess.hpp"

namespace lifeguard {

struct Detector::Impl {
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter> interpreter;
#ifdef LIFEGUARD_USE_XNNPACK
    TfLiteDelegate* xnnpack = nullptr;
#endif
    int input_w = 320;
    int input_h = 320;

    ~Impl() {
#ifdef LIFEGUARD_USE_XNNPACK
        if (xnnpack) TfLiteXNNPackDelegateDelete(xnnpack);
#endif
    }
};

Detector::Detector(Options opts)
    : impl_(std::make_unique<Impl>()), opts_(std::move(opts)) {}

Detector::~Detector() = default;

bool Detector::init() {
    impl_->model =
        tflite::FlatBufferModel::BuildFromFile(opts_.model_path.c_str());
    if (!impl_->model) {
        std::fprintf(stderr, "[detector] failed to load model: %s\n",
                     opts_.model_path.c_str());
        return false;
    }

    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*impl_->model, resolver);
    if (builder(&impl_->interpreter) != kTfLiteOk || !impl_->interpreter) {
        std::fprintf(stderr, "[detector] failed to build interpreter\n");
        return false;
    }

    // Optional CPU acceleration via the XNNPACK delegate.
#ifdef LIFEGUARD_USE_XNNPACK
    TfLiteXNNPackDelegateOptions xopts = TfLiteXNNPackDelegateOptionsDefault();
    xopts.num_threads = opts_.num_threads;
    impl_->xnnpack = TfLiteXNNPackDelegateCreate(&xopts);
    if (impl_->xnnpack &&
        impl_->interpreter->ModifyGraphWithDelegate(impl_->xnnpack) !=
            kTfLiteOk) {
        std::fprintf(stderr, "[detector] XNNPACK delegate unavailable, "
                     "falling back to plain CPU\n");
    }
#endif

    if (impl_->interpreter->AllocateTensors() != kTfLiteOk) {
        std::fprintf(stderr, "[detector] AllocateTensors failed\n");
        return false;
    }

    // Cache input geometry (NHWC).
    const int in_idx = impl_->interpreter->inputs()[0];
    const TfLiteTensor* in = impl_->interpreter->tensor(in_idx);
    if (in->dims->size == 4) {
        impl_->input_h = in->dims->data[1];
        impl_->input_w = in->dims->data[2];
    }
    std::fprintf(stderr, "[detector] initialized: input=%dx%d type=%d outputs=%zu\n",
                 impl_->input_w, impl_->input_h, static_cast<int>(in->type),
                 impl_->interpreter->outputs().size());
    for (size_t i = 0; i < impl_->interpreter->outputs().size(); ++i) {
        const TfLiteTensor* output = impl_->interpreter->output_tensor(i);
        std::fprintf(stderr, "[detector] output[%zu] type=%d bytes=%zu\n", i,
                     output ? static_cast<int>(output->type) : -1,
                     output ? output->bytes : 0);
    }
    return true;
}

std::vector<Detection> Detector::detect(const Frame& frame) {
    std::vector<Detection> out;
    if (!impl_->interpreter || frame.empty()) return out;

    // --- Pre-process into the input tensor -----------------------------
    Preprocess::Options popt;
    popt.width = impl_->input_w;
    popt.height = impl_->input_h;
    popt.to_rgb = true;
    popt.keep_aspect = true;
    popt.quantized_uint8 = true;  // assumes an int8/uint8 model
    Preprocess pre(popt);

    cv::Mat input;
    const Preprocess::Transform tf = pre.run(frame.image, input);

    const int in_idx = impl_->interpreter->inputs()[0];
    TfLiteTensor* in = impl_->interpreter->tensor(in_idx);
    const size_t bytes = input.total() * input.elemSize();
    if (in->bytes >= bytes) {
        std::memcpy(in->data.raw, input.data, bytes);
    }

    if (impl_->interpreter->Invoke() != kTfLiteOk) {
        std::fprintf(stderr, "[detector] Invoke failed\n");
        return out;
    }

    // --- Parse outputs -------------------------------------------------
    // NOTE(bring-up): output layout is model-specific. This parses the
    // standard TFLite Object-Detection API (4 outputs: boxes[1,N,4] in
    // normalized ymin,xmin,ymax,xmax; classes[1,N]; scores[1,N]; count[1]).
    // Adjust for a YOLO-style single-tensor output when you swap models.
    const auto& outs = impl_->interpreter->outputs();
    if (outs.size() < 4) {
        std::fprintf(stderr, "[detector] unexpected output count %zu; "
                             "update detect() for your model\n",
                     outs.size());
        return out;
    }

    const float* boxes = impl_->interpreter->typed_output_tensor<float>(0);
    const float* classes = impl_->interpreter->typed_output_tensor<float>(1);
    const float* scores = impl_->interpreter->typed_output_tensor<float>(2);
    const float* count = impl_->interpreter->typed_output_tensor<float>(3);
    if (!boxes || !scores || !count) return out;

    const int n = static_cast<int>(count[0]);
    static bool reported_output = false;
    if (!reported_output) {
        reported_output = true;
        std::fprintf(stderr, "[detector] first output: count=%.1f score0=%.3f class0=%.1f\n",
                     count[0], scores[0], classes ? classes[0] : -1.0f);
        for (int i = 0; i < std::min(n, 10); ++i) {
            std::fprintf(stderr, "[detector] candidate[%d] score=%.3f class=%.1f\n",
                         i, scores[i], classes ? classes[i] : -1.0f);
        }
    }
    const int model_w = impl_->input_w;
    const int model_h = impl_->input_h;

    for (int i = 0; i < n; ++i) {
        if (scores[i] < opts_.score_threshold) continue;
        const int cls = classes ? static_cast<int>(classes[i]) : 0;
        // Keep only the person class (COCO id 0 for many exports).
        if (cls != opts_.person_class_id) continue;

        // Normalized box -> model pixels -> undo letterbox -> frame pixels.
        const float ymin = boxes[i * 4 + 0] * model_h;
        const float xmin = boxes[i * 4 + 1] * model_w;
        const float ymax = boxes[i * 4 + 2] * model_h;
        const float xmax = boxes[i * 4 + 3] * model_w;

        Detection det;
        det.score = scores[i];
        det.class_id = cls;
        det.box.x = (xmin - tf.pad_x) / tf.scale;
        det.box.y = (ymin - tf.pad_y) / tf.scale;
        det.box.width = (xmax - xmin) / tf.scale;
        det.box.height = (ymax - ymin) / tf.scale;
        out.push_back(det);
    }

    return out;
}

}  // namespace lifeguard
