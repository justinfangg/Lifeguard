#!/bin/sh
set -eu

CONFIG="${1:-config/lifeguard.conf}"
test -f "$CONFIG" || { echo "missing config: $CONFIG" >&2; exit 1; }

value() {
  sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$CONFIG" | head -1
}

backend=$(value camera_backend)
video=$(value video_file)
camera=$(value camera_device)
detector=$(value detector_model)
pose=$(value pose_model)

for path in "$detector" "$pose"; do
  test -f "$path" || { echo "missing required model: $path" >&2; exit 1; }
done

if [ "$backend" = "file" ]; then
  test -f "$video" || { echo "missing video: $video" >&2; exit 1; }
  echo "input: file $video"
else
  test -n "$camera" || { echo "camera_device is empty" >&2; exit 1; }
  echo "input: $backend $camera"
fi
echo "detector: $detector"
echo "MoveNet:  $pose"
echo "runtime files present"
