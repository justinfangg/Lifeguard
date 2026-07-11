# Models

The TensorFlow Lite models are **not** committed to git (they are large binary
blobs). Fetch or build them and drop the `.tflite` files here.

Expected files (paths referenced by `config/lifeguard.conf`):

| File | Purpose | Suggested source |
|------|---------|------------------|
| `swimmer_detector_int8.tflite` | Person/swimmer detection | Start from a COCO-pretrained SSD-MobileNet or a nano-YOLO exported to TFLite, then **int8-quantize** it. Fine-tune on pool footage for best results. |
| `movenet_lightning_int8.tflite` | Single-person pose (17 keypoints) | MoveNet Lightning (int8) from TF Hub / Kaggle Models. |

## Requirements for on-target performance

- **int8 quantization** is strongly recommended — the Pi 5 runs inference on
  CPU (XNNPACK) under QNX with no GPU/NPU offload.
- Keep detector input small (e.g. 320×320) to hit real-time at your target FPS.
- Verify the detector's output layout matches `src/detector.cpp`
  (`TFLite Object-Detection API`: boxes/classes/scores/count). If you use a
  YOLO-style single-tensor output, adjust the parsing there.

## Fetching

Use `scripts/fetch_models.sh` as a starting point. Because model hosting and
licences vary, that script documents where to download from rather than
hard-coding URLs.

## A note on the detector

There is no large public "distressed swimmer" detection model. The realistic
path is:

1. Use a generic person detector to find swimmers.
2. Run pose estimation per swimmer.
3. Apply the temporal distress logic in `DistressAnalyzer`.

To improve accuracy, collect and label your own pool footage (with consent) and
fine-tune the detector, and/or replace the rule-based analyzer with a small
temporal classifier trained on keypoint sequences.
