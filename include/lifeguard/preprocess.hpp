#pragma once

#include <opencv2/core.hpp>

#include "lifeguard/frame.hpp"

namespace lifeguard {

// Converts a captured frame into the tensor layout a model expects.
class Preprocess {
public:
    struct Options {
        int width = 320;         // model input width
        int height = 320;        // model input height
        bool to_rgb = true;      // BGR (OpenCV) -> RGB
        bool keep_aspect = true; // letterbox instead of stretching
        // int8 quantization params (from the model). When quantized, output is
        // uint8/int8; otherwise a float32 [0,1] (or mean/std normalized) tensor.
        bool quantized_uint8 = true;
        float mean = 0.0f;
        float scale = 1.0f / 255.0f;
    };

    explicit Preprocess(Options opts) : opts_(opts) {}

    // Resize/letterbox `src` into `dst` (CV_8UC3 or CV_32FC3 depending on
    // opts_.quantized_uint8). `scale`/`pad` describe the letterbox transform so
    // detections can be mapped back to the original frame coordinates.
    struct Transform {
        float scale = 1.0f;  // src_px * scale = model_px
        int pad_x = 0;
        int pad_y = 0;
    };

    Transform run(const cv::Mat& src, cv::Mat& dst) const;

    const Options& options() const { return opts_; }

private:
    Options opts_;
};

}  // namespace lifeguard
