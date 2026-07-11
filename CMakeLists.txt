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
#   * OpenCV 4 or newer -> pass -DOpenCV_DIR=<...>/lib/cmake/opencv4
#   * TensorFlow Lite -> pass -DTFLITE_ROOT=<tflite install prefix>
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- Build options ---------------------------------------------------------
option(BUILD_AI_LIFEGUARD "Build the TensorFlow Lite inference application" ON)
option(BUILD_CAMERA_TEST "Build the standalone camera smoke-test tool" ON)

# QNX camera/screen libraries are linked when the real capture backend is
# enabled. Kept optional so the skeleton builds before bring-up.
option(LIFEGUARD_QNX_CAMERA "Link against the QNX camera framework" OFF)

# --- OpenCV ----------------------------------------------------------------
# The QNX dependency build uses OpenCV 4 while current Homebrew supplies 5.
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

# --- Main inference application --------------------------------------------
if(BUILD_AI_LIFEGUARD)
  # Prefer the official source-tree CMake target when supplied. It carries all
  # of TFLite's transitive CMake dependencies (FlatBuffers, Abseil, XNNPACK,
  # etc.); a standalone libtensorflow-lite.a does not.
  set(TFLITE_SOURCE_DIR "" CACHE PATH
    "TensorFlow source root containing tensorflow/lite/CMakeLists.txt")

  if(TFLITE_SOURCE_DIR)
    if(NOT EXISTS "${TFLITE_SOURCE_DIR}/tensorflow/lite/CMakeLists.txt")
      message(FATAL_ERROR
        "TFLITE_SOURCE_DIR must contain tensorflow/lite/CMakeLists.txt: "
        "${TFLITE_SOURCE_DIR}")
    endif()
    # TensorFlow Lite v2.16 vendors FP16, whose CMakeLists declares support
    # back to CMake 3.5. CMake 4 requires this explicit compatibility floor.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
      set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING
        "Compatibility floor for legacy third-party CMake projects" FORCE)
      # FP16 invokes CMake again to fetch PSimd. Pass the setting through to
      # that child process as well.
      set(ENV{CMAKE_POLICY_VERSION_MINIMUM} 3.5)
    endif()
    add_subdirectory(
      "${TFLITE_SOURCE_DIR}/tensorflow/lite"
      "${CMAKE_BINARY_DIR}/tensorflow-lite"
      EXCLUDE_FROM_ALL)
    set(TFLITE_TARGET tensorflow-lite)
  else()
    # TFLite does not always ship a CMake package config, so locate an
    # installed prefix by hand. Override with -DTFLITE_ROOT=<prefix> and
    # (optionally) -DTFLITE_LIB=<path>.
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
      message(FATAL_ERROR
        "TensorFlow Lite not found. Set -DTFLITE_SOURCE_DIR=<tensorflow source> "
        "or -DTFLITE_ROOT=<prefix>.\n"
        "  headers: ${TFLITE_INCLUDE_DIR}\n"
        "  library: ${TFLITE_LIB}")
    endif()
    set(TFLITE_TARGET ${TFLITE_LIB})
  endif()

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
    ${OpenCV_INCLUDE_DIRS})

  if(NOT TFLITE_SOURCE_DIR)
    target_include_directories(ai_lifeguard PRIVATE ${TFLITE_INCLUDE_DIR})
  endif()

  target_link_libraries(ai_lifeguard PRIVATE ${OpenCV_LIBS} ${TFLITE_TARGET})

  if(LIFEGUARD_QNX_CAMERA)
    target_compile_definitions(ai_lifeguard PRIVATE LIFEGUARD_QNX_CAMERA=1)
    target_link_libraries(ai_lifeguard PRIVATE camapi screen)
  endif()

  target_compile_options(ai_lifeguard PRIVATE -Wall -Wextra -O2)
  install(TARGETS ai_lifeguard RUNTIME DESTINATION bin)
endif()

# --- Camera smoke-test tool ------------------------------------------------
# A minimal executable that only needs OpenCV (no TensorFlow Lite / models),
# used to verify a physical webcam captures frames. Build both host and QNX.
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
