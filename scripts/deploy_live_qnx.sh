#!/bin/sh
set -eu

PI_IP="${1:-10.0.0.53}"
PI_USER="${PI_USER:-qnxuser}"
REMOTE_DIR="/data/home/qnxuser/lifeguard"
BUNDLE="/tmp/lifeguard-camera-live.tgz"

cd "$(dirname "$0")/.."

echo "Creating camera-only source bundle..."
COPYFILE_DISABLE=1 tar -czf "$BUNDLE" \
  CMakeLists.txt \
  cmake/qnx-toolchain.cmake \
  include/lifeguard/camera_source.hpp \
  include/lifeguard/config.hpp \
  include/lifeguard/frame.hpp \
  include/lifeguard/mjpeg_server.hpp \
  src/camera_source.cpp \
  src/config.cpp \
  src/mjpeg_server.cpp \
  tools/camera_test.cpp \
  config/lifeguard-qnx-camera.conf

echo "Copying to ${PI_USER}@${PI_IP} (enter the Pi password)..."
scp "$BUNDLE" "${PI_USER}@${PI_IP}:/tmp/lifeguard-camera-live.tgz"

echo "Building camera_test directly on the Pi (enter the Pi password again)..."
ssh "${PI_USER}@${PI_IP}" "
  set -e
  mkdir -p '$REMOTE_DIR'
  cd '$REMOTE_DIR'
  tar -xzf /tmp/lifeguard-camera-live.tgz
  cmake -S . -B build-qnx \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DBUILD_AI_LIFEGUARD=OFF \\
    -DBUILD_CAMERA_TEST=ON \\
    -DBUILD_TESTING=OFF \\
    -DLIFEGUARD_QNX_CAMERA=ON \\
    -DLIFEGUARD_CAMERA_TEST_PREVIEW=OFF
  cmake --build build-qnx --target camera_test --parallel 2
  ls -l build-qnx/camera_test
"

echo
echo "Build complete. Next run:"
echo "  ssh -t ${PI_USER}@${PI_IP}"
echo "  su -"
echo "  cd $REMOTE_DIR"
echo "  ./build-qnx/camera_test --config config/lifeguard-qnx-camera.conf --stream-port 8090 --stream-width 960 --jpeg-quality 78 --no-snapshot"
