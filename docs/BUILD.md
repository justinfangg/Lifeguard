# Building AI Lifeguard

There are two build flavours:

1. **Host build** — native OpenCV + TFLite on your Linux dev machine, for
   developing and tuning the pipeline against recorded footage (fastest).
2. **QNX cross build** — for `aarch64le`, to run on the Raspberry Pi 5 / QNX 8.0.

Do the host build first: it lets you validate the whole detect → track → pose →
distress logic with zero QNX risk.

---

## 1. Host build (Linux dev machine)

Install native dependencies (Ubuntu/Debian example):

```bash
sudo apt update
sudo apt install -y build-essential cmake git libopencv-dev
```

For TensorFlow Lite, either install a system package or build it once from
source (same steps as the QNX build below, minus the toolchain file), then
point CMake at it via `-DTFLITE_ROOT=`.

Configure, build, and run against a video clip:

```bash
cmake -B build-host -DCMAKE_BUILD_TYPE=Release -DTFLITE_ROOT=/path/to/tflite
cmake --build build-host -j

# config/lifeguard.conf:
#   camera_backend = file
#   video_file     = /path/to/pool_clip.mp4
./build-host/ai_lifeguard --config config/lifeguard.conf
```

This is where you verify the detector's output layout matches
`src/detector.cpp` and calibrate the thresholds in `DistressAnalyzer`.

---

## 2. QNX cross build (aarch64le)

Prerequisites on the Linux host:

- **QNX SDP 8.0** installed.
- `cmake` (>= 3.16) and `git`.

### 2a. Cross-build the dependencies

Source the SDP environment, then run the helper. It cross-builds TensorFlow
Lite (with the XNNPACK CPU delegate) and OpenCV into `./deps-qnx`:

```bash
source ~/qnx800/qnxsdp-env.sh          # sets QNX_HOST / QNX_TARGET
scripts/build_deps_qnx.sh
```

Useful overrides:

```bash
# Only rebuild TFLite, into a custom prefix, with pinned versions:
BUILD_OPENCV=0 DEPS_PREFIX=$PWD/deps-qnx TF_VERSION=v2.16.1 \
  scripts/build_deps_qnx.sh
```

> The dependency cross-build is the trickiest step. If TFLite or OpenCV fail to
> compile against the SDP, build them one at a time (`BUILD_OPENCV=0` /
> `BUILD_TFLITE=0`) and read the first CMake/compiler error — usually a missing
> QNX-side library or an option that needs disabling.

### 2b. Build the app

```bash
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DOpenCV_DIR=$PWD/deps-qnx/lib/cmake/opencv4 \
      -DTFLITE_ROOT=$PWD/deps-qnx
cmake --build build -j
```

To link the QNX camera framework for live UVC capture, add
`-DLIFEGUARD_QNX_CAMERA=ON` (do this once `UvcCameraSource` is implemented).

### 2c. Deploy to the Pi 5

```bash
PI=root@<pi5-ip>
ssh $PI 'mkdir -p /data/lifeguard/models'
scp build/ai_lifeguard config/lifeguard.conf $PI:/data/lifeguard/
scp deps-qnx/lib/libopencv_*.so.* $PI:/data/lifeguard/   # if shared OpenCV
scp models/*.tflite $PI:/data/lifeguard/models/
```

On the target:

```bash
cd /data/lifeguard
export LD_LIBRARY_PATH=/data/lifeguard:$LD_LIBRARY_PATH
./ai_lifeguard --config lifeguard.conf
```

Start with `camera_backend = file` and a clip copied to the Pi to confirm
inference works on QNX, then switch to `uvc` once the camera backend is done.

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---------|--------------------|
| `TensorFlow Lite not found` at configure | Set `-DTFLITE_ROOT=` to the prefix that contains `include/tensorflow/lite/...` and `lib/libtensorflow-lite.a`. |
| Link errors for `XNNPACK` / `pthreadpool` / `cpuinfo` | Those static libs weren't staged; check `deps-qnx/lib/` and copy any missing `*.a` from the TFLite build tree. |
| `[camera:uvc] built without LIFEGUARD_QNX_CAMERA` | Rebuild with `-DLIFEGUARD_QNX_CAMERA=ON` on the target. |
| Detector returns nothing / wrong boxes | Output layout mismatch — verify your model against the parsing in `src/detector.cpp`. |
| Slow FPS on target | Use int8 models, shrink detector input (e.g. 320×320), raise `num_threads` in the config. |
