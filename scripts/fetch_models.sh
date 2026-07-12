#!/bin/sh
# Fetch / stage the TFLite models AI Lifeguard needs.
#
#   sh scripts/fetch_models.sh
#
# Produces (paths referenced by config/lifeguard.conf):
#   models/swimmer_detector_int8.tflite   (person detector — downloaded here)
#   models/movenet_lightning_int8.tflite  (MoveNet Lightning pose)
#
# The detector is a generic COCO person detector (SSD-MobileNet v1, uint8),
# whose output layout matches src/detector.cpp (boxes/classes/scores/count) and
# whose "person" class id is 0 (see person_class_id in the config). The app runs
# with just this model; pose only adds two of the four distress features.

set -e
MODEL_DIR="$(cd "$(dirname "$0")/../models" && pwd)"
echo "Model directory: $MODEL_DIR"

fetch() {  # fetch <url> <dest>
    if command -v curl >/dev/null 2>&1; then curl -L -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then wget -O "$2" "$1"
    else echo "error: need curl or wget" >&2; return 1
    fi
}

# --- Person / swimmer detector (int8) ---------------------------------
DET_DST="$MODEL_DIR/swimmer_detector_int8.tflite"
DET_URL="https://storage.googleapis.com/download.tensorflow.org/models/tflite/coco_ssd_mobilenet_v1_1.0_quant_2018_06_29.zip"
if [ -f "$DET_DST" ]; then
    echo "detector already present: $DET_DST"
else
    echo "Downloading COCO SSD-MobileNet detector..."
    TMP="$(mktemp -d)"
    fetch "$DET_URL" "$TMP/detector.zip"
    unzip -o "$TMP/detector.zip" -d "$TMP" >/dev/null
    # The archive ships detect.tflite (+ labelmap.txt).
    cp "$TMP/detect.tflite" "$DET_DST"
    rm -rf "$TMP"
    echo "detector -> $DET_DST"
fi

# --- MoveNet Lightning pose --------------------------------------------
POSE_DST="$MODEL_DIR/movenet_lightning_int8.tflite"
if [ -f "$POSE_DST" ]; then
    echo "pose model present: $POSE_DST"
else
    echo "Downloading MoveNet Lightning pose model..."
    fetch 'https://tfhub.dev/google/lite-model/movenet/singlepose/lightning/3?lite-format=tflite' "$POSE_DST"
    echo "pose -> $POSE_DST"
fi

echo "Done."
