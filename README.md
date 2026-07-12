# AI Lifeguard

AI Lifeguard watches the Raspberry Pi 5 Camera Module 3 live feed, detects and
tracks swimmers with TensorFlow Lite, runs MoveNet pose estimation, and shows
potential distress with persistent red boxes and a red status banner.

> **Safety:** this is an experimental visual aid, not a validated drowning
> detector and not a substitute for a trained lifeguard.

## Inspiration

The idea started with a lived experience. One of our teammates spent summers
working as a lifeguard, and knew firsthand the impossible math of the job: a
single pair of eyes scanning a crowded pool, where a drowning can happen
silently in under 30 seconds, often within arm's reach of other swimmers. Real
drowning rarely looks like the dramatic splashing people expect — it's the
quiet, vertical "Instinctive Drowning Response," which is exactly what's easy to
miss.

When our group started exploring what **QNX** — a real-time operating system
built for safety-critical systems like cars and medical devices — could do
beyond its usual domains, that lifeguard experience clicked into place. Human
attention has hard biological limits: fatigue, blind spots, distraction, and the
sheer impossibility of watching fifty people at once. An AI system has none of
those. It can track every swimmer in the pool simultaneously, every frame,
without ever looking away. We built AI Lifeguard to point serious, dependable
technology at a genuinely present, life-or-death problem — not to replace the
lifeguard, but to give them a tireless second set of eyes.

## What it does

AI Lifeguard is an AI-powered water-safety system that turns a live pool camera
feed into a real-time distress-monitoring dashboard for lifeguards.

Concretely, it:

- **Watches the whole pool at once** — continuously detecting and tracking every
  swimmer in frame, each with a persistent identity, so no one drops off the
  radar.
- **Reads body language, not just position** — running pose estimation on each
  swimmer to understand posture and movement, not merely where they are.
- **Recognizes the signature of distress** — scoring interpretable danger
  signals drawn from real drowning behavior: a vertical torso, the head sinking
  low toward the waterline, an absence of forward progress, and vertical
  bobbing.
- **Waits before it cries wolf** — only escalating to an alert once the distress
  pattern *persists* for a swimmer over time, so a single odd frame doesn't
  trigger a false alarm.
- **Makes the emergency impossible to miss** — surfacing a live annotated
  dashboard with green tracking boxes, skeleton overlays, and held red
  `POTENTIAL DANGER` / `DROWNING ALERT` markers, a persistent status banner, and
  a swimmer count, all viewable in a browser and recorded to video for review.

The result is a dependable real-time tool that lets a lifeguard instantly
pinpoint *who* is in trouble and *where* — augmenting their judgment rather than
replacing it.

## How we built it

Building AI Lifeguard meant engineering a complete pipeline from the camera lens
all the way to the lifeguard's screen — with deliberate decisions at every
layer.

- **Capture (QNX + Raspberry Pi):** A **Raspberry Pi 5 with Camera Module 3**
  runs the capture side on **QNX**, using the QNX Sensor Framework to pull frames
  at roughly **28 FPS at 2304×1296**. The Pi's job is intentionally narrow —
  grab frames and stream them out as MJPEG — which keeps the safety-critical edge
  device lean.
- **Transport (Pi → workstation):** Rather than build TensorFlow for QNX, we
  split the system: the Pi streams video over an **SSH tunnel** to a workstation
  that hosts the heavy AI. This turns a limitation into a clean architectural
  boundary.
- **Processing (OpenCV + TensorFlow Lite):** On the workstation, **OpenCV**
  decodes the stream and a threaded pipeline runs the intelligence — a **TFLite
  SSD-MobileNet** detector finds swimmers, **Google's MoveNet** estimates each
  one's pose, a lightweight **IOU-based (SORT-lite) tracker** keeps identities
  stable across frames, and a custom **distress analyzer** fuses posture and
  motion into a debounced risk score. Everything runs on **int8-quantized models
  on CPU via the XNNPACK delegate**, so no GPU is required.
- **Concurrency (so it never stalls):** Capture and inference run on **separate
  threads** joined by a fixed-size, **drop-oldest ring buffer**. If inference
  briefly falls behind the camera, we discard stale frames instead of letting
  latency pile up — the AI always analyzes the freshest view of the pool.
- **Interface (the dashboard):** Finally, an **MJPEG server** streams an
  annotated view to any browser — boxes, MoveNet skeletons, held danger
  overlays, a status banner, swimmer count, and live AI FPS — with the annotated
  footage also saved to MP4 for after-the-fact review.

In short, the project is genuinely **end-to-end**: hardware capture, real-time OS
integration, network transport, multi-stage computer vision, and a human-facing
alerting UI, all wired into one working system.

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

The key thresholds can be tuned there. The defaults require multiple danger
signals before showing red (`potential_distress_score_threshold = 0.60`) and a
score of 0.75 sustained for five seconds before declaring a drowning alert.
