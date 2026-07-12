#!/bin/sh
set -eu

BUILD_DIR="${1:-build-host}"
test -x "$BUILD_DIR/camera_test" || { echo "missing $BUILD_DIR/camera_test" >&2; exit 1; }
test -x "$BUILD_DIR/ai_lifeguard" || { echo "missing $BUILD_DIR/ai_lifeguard" >&2; exit 1; }

"$BUILD_DIR/camera_test" \
  --config config/lifeguard-video.conf \
  --stream-port 8090 --frames "${VIDEO_TEST_FRAMES:-180}" --no-snapshot &
sender=$!
trap 'kill "$sender" 2>/dev/null || true' EXIT INT TERM
sleep 1

"$BUILD_DIR/ai_lifeguard" \
  --config config/lifeguard-video.conf \
  --camera http://127.0.0.1:8090/video.mjpg \
  --headless
wait "$sender" 2>/dev/null || true
