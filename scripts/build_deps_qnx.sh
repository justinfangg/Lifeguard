#!/usr/bin/env bash
#
# Build the AI Lifeguard dependencies (OpenCV + TensorFlow Lite) for
# QNX SDP 8.0 / aarch64 using the OFFICIAL qnx-ports build-files flow —
# the same one the QNX "ai-camera-app" sample uses.
#
#   https://github.com/qnx-ports/build-files
#
# IMPORTANT: QNX ports build from a **Linux host only**. On macOS or Windows,
# run this inside the QNX build Docker container:
#
#     git clone https://github.com/qnx-ports/build-files.git
#     cd build-files/docker
#     ./docker-build-qnx-image.sh
#     ./docker-create-container.sh      # drops you into an Ubuntu container
#     # ...then run this script inside the container.
#
# Prerequisites inside the Linux host/container:
#   * QNX SDP 8.0 installed and licensed, environment sourced
#   * python3.11 (+venv), gfortran, cmake, git
#   * QNX package com.qnx.qnx800.target.screen.img_codecs (for OpenCV)
#
# Usage:
#   source ~/qnx800/qnxsdp-env.sh
#   scripts/build_deps_qnx.sh [tflite] [opencv] [all]
#
# Artifacts are shared libraries (.so) — copy them to the target and link the
# app against them (see docs/BUILD.md).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Where the port source repos and build-files get cloned / installed.
WORKSPACE="${QNX_WORKSPACE:-$REPO_ROOT/third_party/qnx_workspace}"
STAGING="${QNX_STAGING:-$REPO_ROOT/third_party/qnx-staging}"
JLEVEL="${JLEVEL:-4}"

# --- Pre-flight -------------------------------------------------------------
if [[ -z "${QNX_HOST:-}" || -z "${QNX_TARGET:-}" ]]; then
    echo "error: QNX_HOST / QNX_TARGET not set — run 'source ~/qnx800/qnxsdp-env.sh'." >&2
    exit 1
fi
if [[ "$(uname -s)" != "Linux" ]]; then
    echo "warning: qnx-ports builds only from Linux. On macOS/Windows use the" >&2
    echo "         QNX build Docker container (see header) and run this inside it." >&2
fi
command -v cmake >/dev/null || { echo "error: cmake not found" >&2; exit 1; }
command -v git   >/dev/null || { echo "error: git not found"   >&2; exit 1; }

mkdir -p "$WORKSPACE" "$STAGING"
cd "$WORKSPACE"

clone_once() { [[ -d "$2" ]] || git clone "$1" "$2"; }

ensure_common() {
    clone_once https://github.com/qnx-ports/build-files.git build-files
    clone_once https://github.com/qnx-ports/numpy.git numpy
    ( cd numpy && git submodule update --init --recursive )
}

build_numpy() {
    echo "==> numpy (dependency)"
    PREFIX="/usr" QNX_PROJECT_ROOT="$WORKSPACE/numpy" \
        make -C build-files/ports/numpy install -j"$JLEVEL"
}

# --- TensorFlow Lite --------------------------------------------------------
build_tflite() {
    ensure_common
    clone_once https://github.com/qnx-ports/tensorflow.git tensorflow

    echo "==> host flatc (build tool used by TFLite)"
    if [[ ! -x "$WORKSPACE/flatc-native-build/flatbuffers-flatc/bin/flatc" ]]; then
        mkdir -p flatc-native-build
        ( cd flatc-native-build && \
          cmake ../tensorflow/tensorflow/lite/tools/cmake/native_tools/flatbuffers && \
          cmake --build . )
    fi

    build_numpy

    echo "==> TensorFlow Lite for aarch64le"
    QNX_PROJECT_ROOT="$WORKSPACE/tensorflow" \
    QNX_PATCH_DIR="$WORKSPACE/build-files/ports/tensorflow/patches" \
    TFLITE_HOST_TOOLS_DIR="$WORKSPACE/flatc-native-build/flatbuffers-flatc/bin/" \
        make -C build-files/ports/tensorflow install JLEVEL="$JLEVEL"

    echo "    TFLite .so files under:"
    echo "      $WORKSPACE/build-files/ports/tensorflow/nto-aarch64-le/build/"
}

# --- OpenCV -----------------------------------------------------------------
build_opencv() {
    ensure_common
    clone_once https://github.com/qnx-ports/opencv.git opencv
    clone_once https://github.com/qnx-ports/muslflt.git muslflt

    echo "==> muslflt (consistent math; recommended before OpenCV)"
    make -C build-files/ports/muslflt/ \
        INSTALL_ROOT_nto="$STAGING" USE_INSTALL_ROOT=true install \
        QNX_PROJECT_ROOT="$WORKSPACE/muslflt" -j"$JLEVEL"

    build_numpy

    echo "==> OpenCV for aarch64le"
    BUILD_TESTING="OFF" QNX_PROJECT_ROOT="$WORKSPACE/opencv" \
        make -C build-files/ports/opencv \
        INSTALL_ROOT_nto="$STAGING" USE_INSTALL_ROOT=true install -j"$JLEVEL"

    echo "    OpenCV installed under: $STAGING/aarch64le/usr/local"
}

# --- Dispatch ---------------------------------------------------------------
targets=("${@:-all}")
for t in "${targets[@]}"; do
    case "$t" in
        tflite) build_tflite ;;
        opencv) build_opencv ;;
        all)    build_tflite; build_opencv ;;
        *) echo "unknown target: $t (expected: tflite | opencv | all)" >&2 ;;
    esac
done

cat <<EOF

Done. Configure the app pointing at the built dependencies, e.g.:
  -DTFLITE_ROOT=$WORKSPACE/build-files/ports/tensorflow/nto-aarch64-le/build
  -DOpenCV_DIR=$STAGING/aarch64le/usr/local/lib/cmake/opencv4

Remember to copy the .so files to the target (see docs/BUILD.md).
EOF
