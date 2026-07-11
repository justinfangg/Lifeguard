#pragma once

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "lifeguard/frame.hpp"
#include "lifeguard/preprocess.hpp"

namespace lifeguard {

// One detected person/swimmer in original-frame pixel coordinates.
struct Detection {
    cv::Rect2f box;   // x, y, w, h in source-frame pixels
    float score = 0;  // confidence [0,1]
    int class_id = 0; // 0 == person/swimmer
};

// TFLite object detector (MobileNet-SSD / nano-YOLO, int8, XNNPACK delegate).
class Detector {
public:
    struct Options {
        std::string model_path;
        int num_threads = 4;
        float score_threshold = 0.5f;
        float nms_iou_threshold = 0.45f;
    };

    explicit Detector(Options opts);
    ~Detector();

    // Load the model and build the interpreter. Returns false on failure.
    bool init();

    // Run detection on a full frame. Boxes are returned in `frame` pixels.
    std::vector<Detection> detect(const Frame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Options opts_;
};

}  // namespace lifeguard
