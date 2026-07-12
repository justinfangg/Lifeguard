# AI Lifeguard

AI Lifeguard watches the Raspberry Pi 5 Camera Module 3 live feed, detects and
tracks swimmers with TensorFlow Lite, runs MoveNet pose estimation, and shows
potential distress with persistent red boxes and a red status banner.

> **Safety:** this is an experimental visual aid, not a validated drowning
> detector and not a substitute for a trained lifeguard.

## Live-video design

```text
Pi Camera Module 3 -> QNX camera_test MJPEG -> SSH tunnel -> Mac AI pipeline
                                                            |-> OpenCV window
                                                            |-> annotated MP4
                                                            `-> browser :8080
```

The Pi executable is camera-only. It reuses the QNX Sensor Framework callback
path already verified at 2304x1296 and about 28 FPS. TensorFlow Lite, the
swimmer detector, MoveNet, tracking, and distress analysis all run on the Mac.
This avoids building TensorFlow for QNX.

The annotated display includes:

- green boxes for tracked swimmers;
- MoveNet keypoints and skeleton lines, proving pose inference is running;
- red `POTENTIAL DANGER` boxes for vertical motion, bobbing, low-head posture,
  vertical torso posture, or low forward progress;
- a held red state so danger does not disappear after one frame;
- `DROWNING ALERT` after the configured condition persists;
- an always-visible status banner, swimmer count, and AI FPS.

## Build on the Mac

Use the pinned TensorFlow 2.16.1 source checkout:

```bash
brew install cmake opencv
git submodule update --init tensorflow

cmake -S . -B build-host \
  -DCMAKE_BUILD_TYPE=Release \
  -DTFLITE_SOURCE_DIR="$PWD/tensorflow" \
  -DLIFEGUARD_USE_XNNPACK=OFF
cmake --build build-host -j4
```

Run the recorded-video regression:

```bash
./build-host/ai_lifeguard --config config/lifeguard-video.conf --headless
```

Run the camera-sender/network-input regression using the same MP4:

```bash
sh scripts/test_network_video.sh build-host
```

## Run live

See [docs/BUILD.md](docs/BUILD.md) for the exact Pi sender, SSH tunnel, and Mac
commands. The normal Mac config is [config/lifeguard.conf](config/lifeguard.conf).

The key thresholds can be tuned there. The MVP defaults intentionally show red
early (`potential_distress_score_threshold = 0.18`) and hold it for 2.5 seconds,
while the actual alert remains sustained and stricter.
