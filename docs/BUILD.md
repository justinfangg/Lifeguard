# Build and test the live Pi video pipeline

The working split is:

- **Raspberry Pi/QNX:** capture Camera Module 3 and send MJPEG only;
- **Mac:** receive video, run detector + MoveNet + tracking + distress logic,
  draw overlays, record, and publish the annotated browser view.

The Pi does not need TensorFlow Lite or `ai_lifeguard`.

## 1. Build the Mac AI application

```bash
brew install cmake opencv
git submodule update --init tensorflow

cmake -S . -B build-host \
  -DCMAKE_BUILD_TYPE=Release \
  -DTFLITE_SOURCE_DIR="$PWD/tensorflow" \
  -DLIFEGUARD_USE_XNNPACK=OFF
cmake --build build-host -j4
sh scripts/check_runtime.sh config/lifeguard.conf
```

TensorFlow 2.16.1 is pinned because mixing TensorFlow source with old cached
FlatBuffers/Abseil versions causes compile failures. If changing TensorFlow
versions, use a new empty build directory.

## 2. Build only the Pi camera sender

This is the small replacement for the already-working `camera_test`. It links
OpenCV and QNX `camapi`; it does **not** build or link TensorFlow.

On a machine/container that has the QNX toolchain and the same staged OpenCV
used for the original camera test:

```bash
source ~/qnx800/qnxsdp-env.sh
cmake -S . -B build-qnx-camera \
  -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_AI_LIFEGUARD=OFF \
  -DBUILD_CAMERA_TEST=ON \
  -DBUILD_TESTING=OFF \
  -DLIFEGUARD_QNX_CAMERA=ON \
  -DLIFEGUARD_CAMERA_TEST_PREVIEW=OFF \
  -DOpenCV_DIR=<qnx-opencv-cmake-directory>
cmake --build build-qnx-camera --target camera_test -j4
```

Copy only that binary and config:

```bash
scp build-qnx-camera/camera_test \
  config/lifeguard-qnx-camera.conf \
  root@<PI-IP>:/data/home/qnxuser/lifeguard/
```

There is no way for source changes to alter the old QNX executable already on
the Pi: this one small camera-only binary must be compiled once. The full QNX
TensorFlow SDK/dependency build is not needed.

## 3. Start live video on the Pi

Keep the verified Camera Module 3 Sensor Framework service running with camera
unit 1. Then, on the Pi:

```bash
cd /data/home/qnxuser/lifeguard
chmod +x camera_test
./camera_test \
  --config lifeguard-qnx-camera.conf \
  --stream-port 8090 \
  --stream-width 960 \
  --jpeg-quality 78 \
  --no-snapshot
```

Expected output includes `published units: 1`, the IMX708 viewfinder format,
and `live video URL`.

## 4. Tunnel the Pi stream to the Mac

Use the Pi's numeric IP address; `qnxpi25` did not resolve on the Mac before.
In a second Mac terminal:

```bash
ssh -N -L 8090:127.0.0.1:8090 root@<PI-IP>
```

Confirm raw Pi video in a browser:

```text
http://127.0.0.1:8090/video.mjpg
```

## 5. Run TensorFlow + MoveNet on the Pi video

In a third Mac terminal:

```bash
cd /Users/moses/Lifeguard
./build-host/ai_lifeguard --config config/lifeguard.conf
```

The local OpenCV window shows the result. The same annotated feed is available
at:

```text
http://127.0.0.1:8080/video.mjpg
```

Press `q`, `Esc`, or Ctrl-C to stop. The annotated recording is written to
`videos/lifeguard_live_annotated.mp4`.

## Fast local verification

Before using the Pi, exercise the exact MJPEG transport and inference path with
the included MP4:

```bash
sh scripts/test_network_video.sh build-host
```
