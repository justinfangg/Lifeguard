# QNX SDP 8.0 aarch64 (aarch64le) CMake toolchain file.
#
# Usage:
#   source ~/qnx800/qnxsdp-env.sh          # sets QNX_HOST / QNX_TARGET
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake ...
#
# Relies on the standard QNX environment variables exported by
# qnxsdp-env.sh: QNX_HOST and QNX_TARGET.

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{QNX_HOST} OR NOT DEFINED ENV{QNX_TARGET})
  message(FATAL_ERROR
    "QNX_HOST / QNX_TARGET are not set. Run 'source <sdp>/qnxsdp-env.sh' first.")
endif()

set(QNX_HOST   "$ENV{QNX_HOST}")
set(QNX_TARGET "$ENV{QNX_TARGET}")

set(arch gcc_ntoaarch64le)

if(CMAKE_HOST_WIN32)
  set(HOST_EXECUTABLE_SUFFIX ".exe")
endif()

set(CMAKE_C_COMPILER   "${QNX_HOST}/usr/bin/qcc${HOST_EXECUTABLE_SUFFIX}")
set(CMAKE_C_COMPILER_TARGET ${arch})
set(CMAKE_CXX_COMPILER "${QNX_HOST}/usr/bin/q++${HOST_EXECUTABLE_SUFFIX}")
set(CMAKE_CXX_COMPILER_TARGET ${arch})

set(CMAKE_SYSROOT "${QNX_TARGET}")

# Search for headers/libs in the target sysroot only.
set(CMAKE_FIND_ROOT_PATH "${QNX_TARGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
