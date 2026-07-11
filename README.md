# AI Lifeguard (QNX 8.0 · Raspberry Pi 5)

An embedded QNX application that watches a swimming pool through a camera and
raises an alert when it detects a **distressed / drowning swimmer**.

It runs a real-time computer-vision pipeline built on **OpenCV** and
**TensorFlow Lite** (both from [oss.qnx.com](https://oss.qnx.com)) on the
Cortex-A cores of a Raspberry Pi 5 running QNX SDP 8.0.

> ⚠️ **Safety notice.** This is experimental software and **must not** be relied
> upon as a substitute for a trained human lifeguard. Treat any alert as an
> assist, never as a guarantee.

---

## How it works

```
 Camera ──► Capture ──► Ring buffer ──► Pre-process ──► Detect swimmers
                                                              │
                                                              ▼
   Alarm ◄── Distress logic ◄── Temporal window ◄── Pose / behaviour
```

| Stage | Component | Tech |
|-------|-----------|------|
| Capture | `CameraSource` | QNX camera framework (UVC) / CSI |
| Buffering | `RingBuffer` | lock-free SPSC queue |
| Pre-process | `Preprocess` | OpenCV resize / normalize |
| Detect swimmers | `Detector` | TFLite (MobileNet-SSD / nano-YOLO, int8) |
| Track | `Tracker` | IOU/SORT-style ID association |
| Pose | `PoseEstimator` | TFLite MoveNet Lightning (int8) |
| Behaviour | `DistressAnalyzer` | temporal rule-based scorer + debounce |
| Alert | `Alerter` | GPIO buzzer / log / network event |

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design and the
reasoning behind the drowning-detection approach.

## Distress detection approach

Drowning rarely looks like the movies. The **Instinctive Drowning Response** is
near-silent and lasts only 20–60 seconds. We look for a *combination* of signals
sustained over several seconds for the same tracked person:

- torso close to **vertical** in the water,
- **head low** / tilted back, mouth at the water line,
- **little or no forward progress** (low horizontal velocity),
- arms pressing **laterally** rather than stroking,
- repeated **bobbing** (vertical oscillation).

A single frame never triggers an alert — the `DistressAnalyzer` requires the
condition to persist across a sliding time window (debouncing) to keep false
alarms from normal treading/floating low.

## Repository layout

```
Lifeguard/
├── CMakeLists.txt            Top-level build
├── cmake/qnx-toolchain.cmake QNX SDP 8.0 aarch64 toolchain file
├── include/lifeguard/        Public headers (one per pipeline stage)
├── src/                      Implementations + main.cpp
├── config/lifeguard.conf     Runtime configuration
├── models/                   TFLite models (not committed – see scripts)
├── scripts/                  Env setup + model fetch helpers
└── docs/ARCHITECTURE.md      Design notes
```

> See [`docs/BUILD.md`](docs/BUILD.md) for the full host + cross-compile
> workflow, including the `scripts/build_deps_qnx.sh` dependency builder.

## Building (on the host, cross-compiling for QNX aarch64)

Prerequisites:

- QNX SDP 8.0 installed, with the environment sourced:
  ```bash
  source ~/qnx800/qnxsdp-env.sh
  ```
- OpenCV and TensorFlow Lite for QNX aarch64 (`aarch64le`) — either the
  prebuilt packages from oss.qnx.com or your own cross-build. Point CMake at
  them with `-DOpenCV_DIR=...` and `-DTFLITE_ROOT=...`.

```bash
# 1. Configure
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DOpenCV_DIR=$QNX_TARGET/usr/lib/cmake/opencv4 \
      -DTFLITE_ROOT=/path/to/tflite-qnx-aarch64

# 2. Build
cmake --build build -j

# 3. Copy the binary + models + config to the target (example)
scp build/ai_lifeguard config/lifeguard.conf root@<pi5-ip>:/data/lifeguard/
scp models/*.tflite root@<pi5-ip>:/data/lifeguard/models/
```

## Running (on the Pi 5 / QNX)

```bash
# on the target
cd /data/lifeguard
./ai_lifeguard --config lifeguard.conf
```

## Roadmap / status

- [x] Project scaffold, build system, pipeline interfaces
- [ ] Camera capture bring-up (start with USB/UVC, then CSI/IMX708)
- [ ] Wire OpenCV pre-processing to live frames
- [ ] Integrate TFLite detector (XNNPACK delegate, int8)
- [ ] Tracker + MoveNet pose + temporal distress logic
- [ ] Alerting (GPIO / log / network) + watchdog
- [ ] Field calibration & false-alarm tuning

See inline `TODO(bring-up)` markers in the source for the concrete next steps.
