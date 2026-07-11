#include "lifeguard/preprocess.hpp"

#include <opencv2/imgproc.hpp>

namespace lifeguard {

Preprocess::Transform Preprocess::run(const cv::Mat& src, cv::Mat& dst) const {
    Transform tf;
    cv::Mat resized;

    if (opts_.keep_aspect) {
        // Letterbox: scale to fit, pad the remainder with a neutral colour.
        const float scale =
            std::min(static_cast<float>(opts_.width) / src.cols,
                     static_cast<float>(opts_.height) / src.rows);
        const int new_w = static_cast<int>(std::round(src.cols * scale));
        const int new_h = static_cast<int>(std::round(src.rows * scale));
        cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

        tf.scale = scale;
        tf.pad_x = (opts_.width - new_w) / 2;
        tf.pad_y = (opts_.height - new_h) / 2;

        cv::Mat canvas(opts_.height, opts_.width, src.type(),
                       cv::Scalar(114, 114, 114));
        resized.copyTo(canvas(cv::Rect(tf.pad_x, tf.pad_y, new_w, new_h)));
        resized = canvas;
    } else {
        cv::resize(src, resized, cv::Size(opts_.width, opts_.height), 0, 0,
                   cv::INTER_LINEAR);
        tf.scale = static_cast<float>(opts_.width) / src.cols;  // approx
    }

    if (opts_.to_rgb) {
        cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
    }

    if (opts_.quantized_uint8) {
        // Model consumes uint8 directly; keep the CV_8UC3 buffer.
        dst = resized;
    } else {
        resized.convertTo(dst, CV_32FC3, opts_.scale, -opts_.mean * opts_.scale);
    }

    return tf;
}

}  // namespace lifeguard
