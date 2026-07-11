#!/usr/bin/env bash
#
# Cross-build the AI Lifeguard dependencies (TensorFlow Lite and, optionally,
# OpenCV) for QNX 8.0 aarch64 (aarch64le), using the repo's CMake toolchain
# file. Run this on a Linux host with QNX SDP 8.0 installed.
#
# Usage:
#   source ~/qnx800/qnxsdp-env.sh          # sets QNX_HOST / QNX_TARGET
#   scripts/build_deps_qnx.sh              # builds everything into ./deps-qnx
#
# Environment overrides:
#   DEPS_PREFIX     install prefix           (default: <repo>/deps-qnx)
#   BUILD_TFLITE    1/0 build TFLite         (default: 1)
#   BUILD_OPENCV    1/0 build OpenCV         (default: 1)
#   TF_VERSION      TensorFlow git tag       (default: v2.16.1)
#   OPENCV_VERSION  OpenCV git tag           (default: 4.9.0)
#   JOBS            parallel build jobs      (default: nproc)
#
# After it finishes, configure the app with:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \
#         -DOpenCV_DIR=$DEPS_PREFIX/lib/cmake/opencv4 \
#         -DTFLITE_ROOT=$DEPS_PREFIX

set -euo pipefail

# --- Locate the repo + toolchain -----------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLCHAIN="$REPO_ROOT/cmake/qnx-toolchain.cmake"

DEPS_PREFIX="${DEPS_PREFIX:-$REPO_ROOT/deps-qnx}"
BUILD_TFLITE="${BUILD_TFLITE:-1}"
BUILD_OPENCV="${BUILD_OPENCV:-1}"
TF_VERSION="${TF_VERSION:-v2.16.1}"
OPENCV_VERSION="${OPENCV_VERSION:-4.9.0}"
JOBS="${JOBS:-$(nproc)}"

WORK="$REPO_ROOT/.deps-src"

# --- Sanity checks -------------------------------------------------------
if [[ -z "${QNX_HOST:-}" || -z "${QNX_TARGET:-}" ]]; then
    echo "error: QNX_HOST / QNX_TARGET not set." >&2
    echo "       run 'source <sdp>/qnxsdp-env.sh' first." >&2
    exit 1
fi
if [[ ! -f "$TOOLCHAIN" ]]; then
    echo "error: toolchain file not found at $TOOLCHAIN" >&2
    exit 1
fi
for tool in cmake git; do
    command -v "$tool" >/dev/null || { echo "error: '$tool' not found" >&2; exit 1; }
done

echo "=========================================================="
echo " AI Lifeguard dependency cross-build (QNX aarch64le)"
echo "   QNX_HOST    = $QNX_HOST"
echo "   QNX_TARGET  = $QNX_TARGET"
echo "   DEPS_PREFIX = $DEPS_PREFIX"
echo "   TFLite      = $([[ $BUILD_TFLITE == 1 ]] && echo "$TF_VERSION" || echo skip)"
echo "   OpenCV      = $([[ $BUILD_OPENCV == 1 ]] && echo "$OPENCV_VERSION" || echo skip)"
echo "   JOBS        = $JOBS"
echo "=========================================================="

mkdir -p "$WORK" "$DEPS_PREFIX"

# -------------------------------------------------------------------------
# TensorFlow Lite (+ XNNPACK) — built from the TensorFlow tree's CMake project.
# -------------------------------------------------------------------------
build_tflite() {
    echo "--- TensorFlow Lite $TF_VERSION ---"
    local src="$WORK/tensorflow"
    if [[ ! -d "$src" ]]; then
        git clone --depth 1 --branch "$TF_VERSION" \
            https://github.com/tensorflow/tensorflow.git "$src"
    fi

    local build="$WORK/build-tflite"
    cmake -S "$src/tensorflow/lite" -B "$build" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DTFLITE_ENABLE_XNNPACK=ON \
        -DTFLITE_ENABLE_RUY=ON \
        -DTFLITE_ENABLE_GPU=OFF \
        -DBUILD_SHARED_LIBS=OFF
    cmake --build "$build" -j "$JOBS"

    # TFLite's CMake target does not always define an install rule; stage the
    # headers + the static lib into the prefix so the app's find_* can locate
    # them (matches TFLITE_ROOT expectations in the top-level CMakeLists.txt).
    mkdir -p "$DEPS_PREFIX/include" "$DEPS_PREFIX/lib"
    ( cd "$src" && find tensorflow/lite -name '*.h' -print0 \
        | while IFS= read -r -d '' f; do
              install -D "$f" "$DEPS_PREFIX/include/$f"
          done )
    find "$build" -name 'libtensorflow-lite.a' -exec cp {} "$DEPS_PREFIX/lib/" \;
    # XNNPACK + deps static libs (needed at final link time).
    find "$build" \( -name 'libXNNPACK.a' -o -name 'libpthreadpool.a' \
        -o -name 'libcpuinfo.a' -o -name 'libfarmhash.a' \
        -o -name 'libfft2d*.a' -o -name 'libruy*.a' -o -name 'libflatbuffers.a' \) \
        -exec cp {} "$DEPS_PREFIX/lib/" \; 2>/dev/null || true

    echo "TFLite installed under $DEPS_PREFIX"
}

# -------------------------------------------------------------------------
# OpenCV — minimal module set needed by the app.
# -------------------------------------------------------------------------
build_opencv() {
    echo "--- OpenCV $OPENCV_VERSION ---"
    local src="$WORK/opencv"
    if [[ ! -d "$src" ]]; then
        git clone --depth 1 --branch "$OPENCV_VERSION" \
            https://github.com/opencv/opencv.git "$src"
    fi

    local build="$WORK/build-opencv"
    cmake -S "$src" -B "$build" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX" \
        -DBUILD_LIST=core,imgproc,imgcodecs,videoio \
        -DBUILD_SHARED_LIBS=ON \
        -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF \
        -DWITH_FFMPEG=OFF -DWITH_GTK=OFF -DWITH_V4L=OFF \
        -DWITH_1394=OFF -DWITH_OPENEXR=OFF
    cmake --build "$build" -j "$JOBS"
    cmake --install "$build"
    echo "OpenCV installed under $DEPS_PREFIX"
}

[[ "$BUILD_TFLITE" == 1 ]] && build_tflite
[[ "$BUILD_OPENCV" == 1 ]] && build_opencv

echo
echo "Done. Configure the app with:"
echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \\"
echo "        -DOpenCV_DIR=$DEPS_PREFIX/lib/cmake/opencv4 \\"
echo "        -DTFLITE_ROOT=$DEPS_PREFIX"
