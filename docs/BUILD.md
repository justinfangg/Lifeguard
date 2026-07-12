# Building AI Lifeguard

AI Lifeguard runs natively on a machine with OpenCV, TensorFlow Lite, and a
camera device OpenCV can open.

---

## Dependencies

- A C++17 compiler and CMake 3.16+
- **OpenCV** 4 or newer
- **TensorFlow Lite** (built from the TensorFlow source tree via its CMake
  target, which also supplies its transitive dependencies)

```bash
# macOS (Homebrew)
brew install cmake opencv

# Debian / Ubuntu
sudo apt install cmake g++ libopencv-dev

# TensorFlow Lite / LiteRT C++ source
git clone --depth 1 --branch v2.16.1 \
  https://github.com/tensorflow/tensorflow.git third_party/tensorflow
```

## Configure, build, run

```bash
# 1. Configure
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DTFLITE_SOURCE_DIR="$PWD/third_party/tensorflow"

# 2. Build
cmake --build build -j

# 3. Fetch the detector model
sh scripts/fetch_models.sh

# 4. Point the config at a recorded clip and run
#    (edit config/lifeguard.conf: video_file = /path/to/pool_clip.mp4)
./build/ai_lifeguard --config config/lifeguard.conf
```

Already have TensorFlow Lite installed as a prefix instead of a source tree?
Use `-DTFLITE_ROOT=<prefix>` in place of `-DTFLITE_SOURCE_DIR=...`.

## Configuration

All runtime settings live in [config/lifeguard.conf](../config/lifeguard.conf).
The key input setting is:

```ini
video_file = samples/pool.mp4   # recorded footage to analyze
```

The app decodes the file, runs detection → tracking → distress analysis, and
writes alerts to `log_path`. It exits when the video reaches end-of-stream.

Use this loop to validate the detector output layout
([src/detector.cpp](../src/detector.cpp)) and to calibrate the distress
thresholds ([src/distress_analyzer.cpp](../src/distress_analyzer.cpp)).

For the simplest live-video setup, use the host webcam mode in
[config/lifeguard.conf](../config/lifeguard.conf) and set `camera_backend = uvc`
with `camera_device = 0`. On Raspberry Pi OS or Linux, that can be the Pi's
camera if it enumerates as a V4L2/OpenCV device.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the design and the distress-detection
rationale.
