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

# TensorFlow Lite: build once for the host, or use a prebuilt.
# See scripts/build_deps_qnx.sh for the CMake invocation (drop the toolchain
# file to build for the host instead).
```

### Configure, build, run

```bash
cmake -B build-host \
      -DCMAKE_BUILD_TYPE=Release \
      -DOpenCV_DIR="$(brew --prefix opencv)/lib/cmake/opencv4" \
      -DTFLITE_ROOT=/path/to/tflite-host
cmake --build build-host -j

# Point the config at a recorded clip and run:
#   camera_backend = file
#   video_file     = /path/to/pool_clip.mp4
./build-host/ai_lifeguard --config config/lifeguard.conf
```

Use this loop to validate the detector output layout ([src/detector.cpp](../src/detector.cpp))
and to calibrate the distress thresholds ([src/distress_analyzer.cpp](../src/distress_analyzer.cpp)).

---

## 2. QNX cross build (target: Raspberry Pi 5, aarch64le)

### 2.1 Source the SDP

```bash
source ~/qnx800/qnxsdp-env.sh     # exports QNX_HOST and QNX_TARGET
echo "$QNX_HOST"; echo "$QNX_TARGET"
```

### 2.2 Cross-build the dependencies

Use the helper script (installs into `third_party/qnx-aarch64/`):

```bash
scripts/build_deps_qnx.sh all      # TensorFlow Lite + OpenCV
# or individually:
scripts/build_deps_qnx.sh tflite
scripts/build_deps_qnx.sh opencv
```

> The dependency cross-build is the fiddliest part. If TFLite or OpenCV fail to
> configure, check that `qcc`/`q++` are on `PATH` (from `qnxsdp-env.sh`) and that
> `cmake --version` is >= 3.16. Build logs land under `third_party/_build/`.

### 2.3 Build the app

```bash
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DTFLITE_ROOT="$PWD/third_party/qnx-aarch64" \
      -DOpenCV_DIR="$PWD/third_party/qnx-aarch64/lib/cmake/opencv4"
cmake --build build -j
```

To enable the QNX camera-framework capture backend (once you implement it),
add `-DLIFEGUARD_QNX_CAMERA=ON`.

### 2.4 Deploy to the Pi 5

```bash
PI=root@<pi5-ip>
ssh "$PI" 'mkdir -p /data/lifeguard/models'
scp build/ai_lifeguard config/lifeguard.conf "$PI":/data/lifeguard/
scp models/*.tflite "$PI":/data/lifeguard/models/

ssh "$PI" 'cd /data/lifeguard && ./ai_lifeguard --config lifeguard.conf'
```

Run first with `camera_backend = file` (a clip staged on the target) to confirm
inference works on QNX, **then** move to a live USB/UVC camera.

---

## Build order summary

```
host build (file input)  ─►  QNX deps  ─►  QNX app build  ─►  deploy + file test  ─►  UVC camera  ─►  CSI camera
      (Milestone 1)              (────────  Milestone 2  ────────)                   (Milestone 3)
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for the design and the distress-detection
rationale.
