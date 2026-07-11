#!/bin/sh
# Configure the QNX SDP 8.0 build environment for AI Lifeguard.
#
# Usage:  . scripts/setup_env.sh   (source it, don't execute)
#
# Adjust QNX_SDP to where you installed the SDP.

QNX_SDP="${QNX_SDP:-$HOME/qnx800}"

if [ ! -f "$QNX_SDP/qnxsdp-env.sh" ]; then
    echo "error: qnxsdp-env.sh not found under $QNX_SDP" >&2
    echo "       set QNX_SDP to your SDP 8.0 install directory" >&2
    return 1 2>/dev/null || exit 1
fi

# shellcheck disable=SC1091
. "$QNX_SDP/qnxsdp-env.sh"

echo "QNX_HOST   = $QNX_HOST"
echo "QNX_TARGET = $QNX_TARGET"
echo "Ready to configure with:"
echo "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake \\"
echo "        -DOpenCV_DIR=<...> -DTFLITE_ROOT=<...>"
