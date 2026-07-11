#!/bin/sh
# Fetch / stage the TFLite models AI Lifeguard needs.
#
# Model hosting and licences change over time, so this script intentionally
# does not hard-code download URLs. Fill in the URLs (or copy from a local
# mirror) for your environment, then run it from the repo root.
#
#   sh scripts/fetch_models.sh
#
# The resulting files must match the paths in config/lifeguard.conf:
#   models/swimmer_detector_int8.tflite
#   models/movenet_lightning_int8.tflite

set -e
MODEL_DIR="$(cd "$(dirname "$0")/../models" && pwd)"

echo "Model directory: $MODEL_DIR"

# --- MoveNet Lightning (int8) -----------------------------------------
# Available from TF Hub / Kaggle Models. Download the int8 .tflite and save as:
MOVENET_DST="$MODEL_DIR/movenet_lightning_int8.tflite"
# Example:
#   curl -L -o "$MOVENET_DST" "<MOVENET_INT8_TFLITE_URL>"
if [ ! -f "$MOVENET_DST" ]; then
    echo "TODO: download MoveNet Lightning int8 -> $MOVENET_DST"
fi

# --- Swimmer / person detector (int8) ---------------------------------
# Start from an SSD-MobileNet or nano-YOLO exported to TFLite and int8
# quantized. Optionally fine-tune on pool footage. Save as:
DET_DST="$MODEL_DIR/swimmer_detector_int8.tflite"
#   curl -L -o "$DET_DST" "<DETECTOR_INT8_TFLITE_URL>"
if [ ! -f "$DET_DST" ]; then
    echo "TODO: download / build swimmer detector int8 -> $DET_DST"
fi

echo "Done. Verify both .tflite files exist in $MODEL_DIR before running."
