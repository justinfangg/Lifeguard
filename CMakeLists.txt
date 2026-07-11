cmake_minimum_required(VERSION 3.16)
project(ai_lifeguard LANGUAGES CXX)

# ---------------------------------------------------------------------------
# AI Lifeguard — QNX 8.0 / Raspberry Pi 5
#
# Cross-compiled for QNX aarch64 (aarch64le). Configure with the bundled
# toolchain file:
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-toolchain.cmake ...
#
# Dependencies (from oss.qnx.com or your own cross-build):
#   * OpenCV 4      -> pass -DOpenCV_DIR=<...>/lib/cmake/opencv4
#   * TensorFlow Lite -> pass -DTFLITE_ROOT=<tflite install prefix>
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- OpenCV ----------------------------------------------------------------
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs)

# --- TensorFlow Lite -------------------------------------------------------
# TFLite does not always ship a CMake package config, so we locate it by hand.
# Override with -DTFLITE_ROOT=<prefix> and (optionally) -DTFLITE_LIB=<path>.
set(TFLITE_ROOT "" CACHE PATH "TensorFlow Lite install prefix (headers + libs)")

find_path(TFLITE_INCLUDE_DIR
  NAMES tensorflow/lite/interpreter.h
  HINTS ${TFLITE_ROOT} ${TFLITE_ROOT}/include
  PATH_SUFFIXES include)

find_library(TFLITE_LIB
  NAMES tensorflow-lite tensorflowlite libtensorflow-lite
  HINTS ${TFLITE_ROOT} ${TFLITE_ROOT}/lib
  PATH_SUFFIXES lib)

if(NOT TFLITE_INCLUDE_DIR OR NOT TFLITE_LIB)
  message(WARNING
    "TensorFlow Lite not found. Set -DTFLITE_ROOT=<prefix>.\n"
    "  headers: ${TFLITE_INCLUDE_DIR}\n"
    "  library: ${TFLITE_LIB}\n"
    "The build will fail to link until these are provided.")
endif()

# --- Target ----------------------------------------------------------------
add_executable(ai_lifeguard
  src/main.cpp
  src/config.cpp
  src/camera_source.cpp
  src/preprocess.cpp
  src/detector.cpp
  src/pose_estimator.cpp
  src/tracker.cpp
  src/distress_analyzer.cpp
  src/alerter.cpp)

target_include_directories(ai_lifeguard PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${OpenCV_INCLUDE_DIRS}
  ${TFLITE_INCLUDE_DIR})

target_link_libraries(ai_lifeguard PRIVATE
  ${OpenCV_LIBS}
  ${TFLITE_LIB})

# QNX camera/screen libraries are linked when the real capture backend is
# enabled. Kept optional so the skeleton builds before bring-up.
option(LIFEGUARD_QNX_CAMERA "Link against the QNX camera framework" OFF)
if(LIFEGUARD_QNX_CAMERA)
  target_compile_definitions(ai_lifeguard PRIVATE LIFEGUARD_QNX_CAMERA=1)
  target_link_libraries(ai_lifeguard PRIVATE camapi screen)
endif()

target_compile_options(ai_lifeguard PRIVATE -Wall -Wextra -O2)

install(TARGETS ai_lifeguard RUNTIME DESTINATION bin)

# --- Camera smoke-test tool ------------------------------------------------
# A minimal executable that only needs OpenCV (no TensorFlow Lite / models),
# used to verify a physical webcam captures frames. Build both host and QNX.
option(BUILD_CAMERA_TEST "Build the standalone camera smoke-test tool" ON)
if(BUILD_CAMERA_TEST)
  add_executable(camera_test
    tools/camera_test.cpp
    src/config.cpp
    src/camera_source.cpp)

  target_include_directories(camera_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS})

  target_link_libraries(camera_test PRIVATE ${OpenCV_LIBS})

  if(LIFEGUARD_QNX_CAMERA)
    target_compile_definitions(camera_test PRIVATE LIFEGUARD_QNX_CAMERA=1)
    target_link_libraries(camera_test PRIVATE camapi screen)
  endif()

  target_compile_options(camera_test PRIVATE -Wall -Wextra -O2)
  install(TARGETS camera_test RUNTIME DESTINATION bin)
endif()
