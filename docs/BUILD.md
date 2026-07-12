# Building AI Lifeguard

Two build paths:

1. **Host build** — native, for developing and tuning the pipeline against
   recorded footage (fastest iteration, no QNX needed).
2. **QNX cross build** — for `aarch64le`, to run on the Raspberry Pi 5.

Do the host build first; it de-risks everything before you fight the toolchain.

---

## 1. Host build (macOS / Linux dev machine)

### Dependencies

```bash
# macOS (Homebrew)
brew install cmake opencv

# TensorFlow Lite / LiteRT C++ source for the host. This project integrates it
# through its official CMake target, which also supplies its dependencies.
git clone --depth 1 --branch v2.16.1 \
  https://github.com/tensorflow/tensorflow.git third_party/tensorflow
```

### Configure, build, run

```bash
cmake -B build-host \
      -DCMAKE_BUILD_TYPE=Release \
      -DTFLITE_SOURCE_DIR="$PWD/third_party/tensorflow"
cmake --build build-host -j

# Point the config at a recorded clip and run:
#   camera_backend = file
#   video_file     = /path/to/pool_clip.mp4
./build-host/ai_lifeguard --config config/lifeguard.conf
```

To build and run only the webcam smoke test, TensorFlow Lite is not needed:

```bash
cmake -S . -B build-host-camera \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_AI_LIFEGUARD=OFF \
      -DBUILD_CAMERA_TEST=ON
cmake --build build-host-camera --target camera_test -j
```

Use this loop to validate the detector output layout ([src/detector.cpp](../src/detector.cpp))
and to calibrate the distress thresholds ([src/distress_analyzer.cpp](../src/distress_analyzer.cpp)).

---

## 2. QNX cross build (target: Raspberry Pi, aarch64le)

> **Platform note.** Boot the Pi 5 image first, then run
> `scripts/probe_qnx_camera.sh` on the target before configuring a live camera.
> The QNX Quick Start Target Image supports Raspberry Pi Camera Module 3 when
> the IMX708 driver and Sensor Framework configuration are present. On a Pi 5,
> connect the camera to the connector labelled `CAM/DISP0`.

### 2.0 Required QNX SDP 8.0 packages

Install via the QNX Software Center (needed for the camera + codecs):

- `com.qnx.qnx800.target.sf.camapi` (Sensor Framework camera API)
- `com.qnx.qnx800.target.sf.base`
- `com.qnx.qnx800.target.mm.aoi`
- `com.qnx.qnx800.target.mm.mmf.core`
- `com.qnx.qnx800.target.screen.img_codecs`

### 2.0.1 Probe the target camera stack

After the Pi 5 boots QNX, copy `scripts/probe_qnx_camera.sh` to it and run:

```bash
scp scripts/probe_qnx_camera.sh root@<pi-ip>:/tmp/
ssh root@<pi-ip> 'sh /tmp/probe_qnx_camera.sh /tmp/lifeguard-qnx-camera-probe.txt'
scp root@<pi-ip>:/tmp/lifeguard-qnx-camera-probe.txt .
```

The report identifies whether the image exposes a camera/Sensor Framework
stack. Do not set `camera_backend = csi` until it shows the IMX708 camera
driver and the Broadcom Pi 5 sensor platform. If either is absent, obtain the
matching QNX target-image components before attempting application capture.

### 2.0.2 Verify Camera Module 3 before deploying the app

Power the Pi off, connect the Camera Module 3 ribbon cable to `CAM/DISP0`, and
boot it. On the Pi's local QNX console, become root and use QNX's supplied
Sensor Framework configuration:

```bash
su -
slay sensor
sensor -U 521:521,1001 -b external -r /data/share/sensor \
  -c /usr/etc/config/sensor/camera_module3.conf
camera_example3_viewfinder
```

The viewfinder must show live frames before deploying AI Lifeguard. Press the
viewfinder's quit key to exit. To keep this configuration after reboot, edit
`/usr/etc/startup/post_startup.sh`: disable the simulator `sensor` command and
enable the Camera Module 3 `sensor` command using the same arguments above.

### 2.1 Source the SDP

```bash
source ~/qnx800/qnxsdp-env.sh     # exports QNX_HOST and QNX_TARGET
echo "$QNX_HOST"; echo "$QNX_TARGET"
```

### 2.2 Cross-build the dependencies (OpenCV + TensorFlow Lite)

These are built with the official **[qnx-ports/build-files](https://github.com/qnx-ports/build-files)**
flow — the same one the QNX `ai-camera-app` sample uses. They produce shared
libraries (`.so`) that get copied to the target.

> **macOS hosts:** the QNX ports build **only on Linux**. Use the QNX build
> Docker container (works on macOS), then run the helper inside it:
>
> ```bash
> git clone https://github.com/qnx-ports/build-files.git
> cd build-files/docker
> ./docker-build-qnx-image.sh
> ./docker-create-container.sh     # drops you into an Ubuntu container
> ```

Inside the Linux host/container (with the SDP sourced):

```bash
scripts/build_deps_qnx.sh all      # TensorFlow Lite + OpenCV (+ numpy, muslflt)
# or individually:
scripts/build_deps_qnx.sh tflite
scripts/build_deps_qnx.sh opencv
```

The script clones the `qnx-ports` forks (tensorflow, opencv, numpy, muslflt)
into `third_party/qnx_workspace/` and installs OpenCV into
`third_party/qnx-staging/`. Expect this to need python3.11 + gfortran and a lot
of RAM (~32 GB recommended for a full-core build).

### 2.3 Build the app

```bash
TFLITE_BUILD=third_party/qnx_workspace/build-files/ports/tensorflow/nto-aarch64-le/build
OPENCV_CMAKE=third_party/qnx-staging/aarch64le/usr/local/lib/cmake/opencv4

cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DLIFEGUARD_QNX_CAMERA=ON \
      -DTFLITE_ROOT="$PWD/$TFLITE_BUILD" \
      -DOpenCV_DIR="$PWD/$OPENCV_CMAKE"
cmake --build build -j
```

`-DLIFEGUARD_QNX_CAMERA=ON` enables the QSF camera capture backend and links
`libcamapi` + `libscreen`.

### 2.4 Deploy to the target

Copy the app, the OpenCV + TFLite `.so` libraries, models and config to the
Pi. The dependency libraries must be on the target's `LD_LIBRARY_PATH`:

```bash
PI=root@<pi-ip>
ssh "$PI" 'mkdir -p /data/lifeguard/models /data/lifeguard/lib'

# app + assets
scp build/ai_lifeguard config/lifeguard.conf "$PI":/data/lifeguard/
scp models/*.tflite "$PI":/data/lifeguard/models/

# dependency shared libraries (adjust globs to what the build produced)
scp third_party/qnx-staging/aarch64le/usr/local/lib/libopencv_* "$PI":/data/lifeguard/lib/
scp third_party/qnx_workspace/build-files/ports/tensorflow/nto-aarch64-le/build/libtensorflow-lite.so "$PI":/data/lifeguard/lib/

ssh "$PI" 'cd /data/lifeguard && LD_LIBRARY_PATH=$PWD/lib ./ai_lifeguard --config lifeguard.conf'
```

Run first with `camera_backend = file` (a clip staged on the target) to confirm
inference works on QNX, **then** switch to the live camera.

---

## Build order summary

```
host build (file input)  ─►  QNX deps  ─►  QNX app build  ─►  deploy + file test  ─►  UVC camera  ─►  CSI camera
      (Milestone 1)              (────────  Milestone 2  ────────)                   (Milestone 3)
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for the design and the distress-detection
rationale.
