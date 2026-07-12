#!/bin/sh
#
# Collect the target-side facts needed before configuring AI Lifeguard for a
# QNX camera. This is intentionally read-only: it neither starts drivers nor
# changes the IFS image.
#
# Run on the QNX target:
#   sh probe_qnx_camera.sh /tmp/lifeguard-qnx-camera-probe.txt
# Copy the output file back to the development host for review.

set -u

OUT="${1:-/tmp/lifeguard-qnx-camera-probe.txt}"

{
  echo "AI Lifeguard QNX camera probe"
  echo "Generated: $(date 2>/dev/null || true)"
  echo

  echo "== OS and board =="
  uname -a 2>&1 || true
  echo

  echo "== Running camera / Sensor Framework processes =="
  if command -v pidin >/dev/null 2>&1; then
    pidin ar 2>&1 | grep -Ei 'camera|sensor|screen|mmf|aoi|capture|imx|csi|rp1' || true
  else
    echo "pidin is not available"
  fi
  echo

  echo "== Candidate device nodes =="
  ls -l /dev 2>&1 | grep -Ei 'camera|video|media|i2c|csi|drm' || true
  echo

  echo "== Installed camera-related files =="
  for dir in /etc /lib /usr/lib /sbin /usr/sbin; do
    if [ -d "$dir" ]; then
      find "$dir" -type f -print 2>/dev/null
    fi
  done | grep -Ei 'camera|sensor|camapi|imx708|csi|rp1|screen|mmf|aoi' || true
  echo

  echo "== Network identity (for deployment) =="
  if command -v ifconfig >/dev/null 2>&1; then
    ifconfig 2>&1 || true
  fi
  if command -v hostname >/dev/null 2>&1; then
    hostname 2>&1 || true
  fi
} >"$OUT" 2>&1

echo "Camera probe written to $OUT"
