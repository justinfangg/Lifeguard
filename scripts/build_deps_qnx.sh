#!/usr/bin/env bash
#
# Cross-build the AI Lifeguard dependencies (TensorFlow Lite and, optionally,
# OpenCV) for QNX SDP 8.0 / aarch64 (aarch64le).
#
# Host: macOS (also works on Linux). Requires: cmake >= 3.16, git, ninja or make.
#
# Usage:
#   source ~/qnx800/qnxsdp-env.sh            # sets QNX_HOST / QNX_TARGET
#   scripts/build_deps_qnx.sh [tflite] [opencv] [all]
#
# Examples:
#   scripts/build_deps_qnx.sh tflite         # just TFLite
#   scripts/build_deps_qnx.sh all            # TFLite + OpenCV
#
# Outputs are installed under:  third_party/qnx-aarch64/{include,lib}
# Point the app build at them with:
#   -DTFLITE_ROOT=<repo>/third_party/qnx-aarch64
#   -DOpenCV_DIR=<repo>/third_party/qnx-aarch64/lib/cmake/opencv4
#
set -euo pipefail

# --- Locations --------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLCHAIN="$REPO_ROOT/cmake/qnx-toolchain.cmake"
WORK_DIR="$REPO_ROOT/third_party/_build"
PREFIX="$REPO_ROOT/third_party/qnx-aarch64"

# Pin versions for reproducibility; bump as needed.
TF_VERSION="v2.16.1"
OPENCV_VERSION="4.9.0"

# --- Pre-flight checks ------------------------------------------------------
if [[ -z "${QNX_HOST:-}" || -z "${QNX_TARGET:-}" ]]; then
    echo "error: QNX_HOST / QNX_TARGET not set." >&2
    echo "       Run 'source ~/qnx800/qnxsdp-env.sh' first." >&2
    exit 1
fi
if [[ ! -f "$TOOLCHAIN" ]]; then
    echo "error: toolchain file not found: $TOOLCHAIN" >&2
    exit 1
fi

command -v cmake >/dev/null || { echo "error: cmake not found" >&2; exit 1; }
command -v git   >/dev/null || { echo "error: git not found"   >&2; exit 1; }

# Prefer ninja if available.
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null; then GENERATOR="Ninja"; fi

JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "$WORK_DIR" "$PREFIX"

# --- TensorFlow Lite --------------------------------------------------------
build_tflite() {
    echo "==> Building TensorFlow Lite ($TF_VERSION) for aarch64le"
    local src="$WORK_DIR/tensorflow"
    if [[ ! -d "$src" ]]; then
        git clone --depth 1 --branch "$TF_VERSION" \
            https://github.com/tensorflow/tensorflow.git "$src"
    fi

    local build="$WORK_DIR/tflite-qnx"
    cmake -S "$src/tensorflow/lite" -B "$build" -G "$GENERATOR" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DTFLITE_ENABLE_XNNPACK=ON \
        -DTFLITE_ENABLE_GPU=OFF \
        -DTFLITE_ENABLE_RUY=ON \
        -DTFLITE_ENABLE_INSTALL=ON \
        -DCMAKE_INSTALL_PREFIX="$PREFIX"

    cmake --build "$build" -j "$JOBS"
    cmake --install "$build" || {
        # Some TFLite versions don't ship an install target; stage by hand.
        echo "    (no install target — staging headers/libs manually)"
        mkdir -p "$PREFIX/include" "$PREFIX/lib"
        rsync -a --include='*/' --include='*.h' --exclude='*' \
            "$src/tensorflow/lite/" "$PREFIX/include/tensorflow/lite/"
        find "$build" -name 'libtensorflow-lite*.a' -exec cp {} "$PREFIX/lib/" \;
    }
    echo "==> TFLite installed under $PREFIX"
}

# --- OpenCV -----------------------------------------------------------------
build_opencv() {
    echo "==> Building OpenCV ($OPENCV_VERSION) for aarch64le"
    local src="$WORK_DIR/opencv"
    if [[ ! -d "$src" ]]; then
        git clone --depth 1 --branch "$OPENCV_VERSION" \
            https://github.com/opencv/opencv.git "$src"
    fi

    local build="$WORK_DIR/opencv-qnx"
    # Minimal module set for this app: core, imgproc, imgcodecs, videoio.
    cmake -S "$src" -B "$build" -G "$GENERATOR" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_LIST=core,imgproc,imgcodecs,videoio \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_opencv_apps=OFF \
        -DWITH_FFMPEG=OFF -DWITH_GTK=OFF -DWITH_V4L=OFF \
        -DWITH_OPENEXR=OFF -DWITH_JASPER=OFF \
        -DBUILD_JPEG=ON -DBUILD_PNG=ON -DBUILD_ZLIB=ON

    cmake --build "$build" -j "$JOBS"
    cmake --install "$build"
    echo "==> OpenCV installed under $PREFIX"
}

# --- Dispatch ---------------------------------------------------------------
targets=("${@:-tflite}")
did_something=0
for t in "${targets[@]}"; do
    case "$t" in
        tflite) build_tflite; did_something=1 ;;
        opencv) build_opencv; did_something=1 ;;
        all)    build_tflite; build_opencv; did_something=1 ;;
        *) echo "unknown target: $t (expected: tflite | opencv | all)" >&2 ;;
    esac
done

if [[ "$did_something" -eq 1 ]]; then
    echo
    echo "Done. Configure the app with:"
    echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \\"
    echo "        -DTFLITE_ROOT=$PREFIX \\"
    echo "        -DOpenCV_DIR=$PREFIX/lib/cmake/opencv4"
fi
