cmake_minimum_required(VERSION 3.16)
project(ai_lifeguard LANGUAGES CXX)

# ---------------------------------------------------------------------------
# AI Lifeguard — software-only build
#
# A native host application that analyzes recorded pool footage (MP4) with a
# computer-vision pipeline built on OpenCV and TensorFlow Lite.
#
# Dependencies:
#   * OpenCV 4 or newer -> pass -DOpenCV_DIR=<...>/lib/cmake/opencv4 if needed
#   * TensorFlow Lite -> pass -DTFLITE_SOURCE_DIR=<tensorflow source> or
#                        -DTFLITE_ROOT=<tflite install prefix>
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Apple Clang 21 deprecates the builtin used by the vendored Abseil release
# pulled in by TensorFlow Lite. Keep this third-party compatibility warning
# from obscuring actual project diagnostics.
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  add_compile_options(-Wno-deprecated-builtins)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- Build options ---------------------------------------------------------
option(BUILD_AI_LIFEGUARD "Build the TensorFlow Lite inference application" ON)
option(TENSORFLOW_OFFLINE "Use only a complete matching local dependency cache" OFF)
option(LIFEGUARD_USE_XNNPACK "Use XNNPACK acceleration when versions match" OFF)

# --- OpenCV ----------------------------------------------------------------
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)
find_package(OpenCV REQUIRED COMPONENTS highgui)

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

    # TensorFlow's Lite CMake files otherwise fetch Abseil, protobuf, XNNPACK,
    # and several smaller libraries from GitHub.  The full TensorFlow checkout
    # already vendors compatible copies of these dependencies, so point
    # FetchContent at them when offline/local builds are requested.
    # Tell Lite where the TensorFlow project itself lives.  This avoids a
    # second, redundant TensorFlow clone; dependency fetching is controlled
    # separately below.
    set(TENSORFLOW_SOURCE_DIR "${TFLITE_SOURCE_DIR}" CACHE PATH
      "Directory containing the TensorFlow project" FORCE)

    if(TENSORFLOW_OFFLINE)
      set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL
        "Do not access the network for TensorFlow dependencies" FORCE)

      # Prefer the TensorFlow checkout, with the existing build cache as a
      # fallback for dependencies whose source is generated/populated there.
      macro(lifeguard_dep name checkout fallback)
        if(EXISTS "${checkout}/CMakeLists.txt")
          set(FETCHCONTENT_SOURCE_DIR_${name} "${checkout}" CACHE PATH "" FORCE)
        elseif(EXISTS "${fallback}")
          set(FETCHCONTENT_SOURCE_DIR_${name} "${fallback}" CACHE PATH "" FORCE)
        endif()
      endmacro()
      lifeguard_dep(ABSEIL-CPP "${TFLITE_SOURCE_DIR}/third_party/absl" "${CMAKE_BINARY_DIR}/abseil-cpp")
      lifeguard_dep(PROTOBUF "${TFLITE_SOURCE_DIR}/third_party/xla/third_party/protobuf" "${CMAKE_BINARY_DIR}/tflite-clean/protobuf")
      lifeguard_dep(EIGEN "${TFLITE_SOURCE_DIR}/third_party/xla/third_party/eigen3" "${CMAKE_BINARY_DIR}/eigen")
      lifeguard_dep(FARMHASH "${TFLITE_SOURCE_DIR}/third_party/xla/third_party/farmhash" "${CMAKE_BINARY_DIR}/farmhash")
      lifeguard_dep(FFT2D "${TFLITE_SOURCE_DIR}/third_party/fft2d" "${CMAKE_BINARY_DIR}/fft2d")
      lifeguard_dep(FLATBUFFERS "${TFLITE_SOURCE_DIR}/third_party/flatbuffers" "${CMAKE_BINARY_DIR}/flatbuffers")
      lifeguard_dep(GEMMLOWP "${TFLITE_SOURCE_DIR}/third_party/xla/third_party/gemmlowp" "${CMAKE_BINARY_DIR}/gemmlowp")
      lifeguard_dep(CPUINFO "${TFLITE_SOURCE_DIR}/third_party/cpuinfo" "${CMAKE_BINARY_DIR}/cpuinfo")
      lifeguard_dep(RUY "${TFLITE_SOURCE_DIR}/third_party/ruy" "${CMAKE_BINARY_DIR}/ruy")
      lifeguard_dep(XNNPACK "${TFLITE_SOURCE_DIR}/third_party/xla/third_party/xnnpack" "${CMAKE_BINARY_DIR}/xnnpack")
      lifeguard_dep(PTHREADPOOL "${TFLITE_SOURCE_DIR}/third_party/xla/third_party/pthreadpool" "${CMAKE_BINARY_DIR}/pthreadpool-source")
      lifeguard_dep(FP16 "${TFLITE_SOURCE_DIR}/third_party/xla/third_party/FP16" "${CMAKE_BINARY_DIR}/FP16-source")
      lifeguard_dep(ML_DTYPES "${TFLITE_SOURCE_DIR}/third_party/py/ml_dtypes" "${CMAKE_BINARY_DIR}/ml_dtypes")
    endif()
    if(NOT LIFEGUARD_USE_XNNPACK)
      set(TFLITE_ENABLE_XNNPACK OFF CACHE BOOL
        "Disable TensorFlow Lite XNNPACK backend" FORCE)
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
    src/mjpeg_server.cpp
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

  if(LIFEGUARD_USE_XNNPACK)
    target_compile_definitions(ai_lifeguard PRIVATE LIFEGUARD_USE_XNNPACK=1)
    if(TARGET xnnpack-delegate)
      target_link_libraries(ai_lifeguard PRIVATE xnnpack-delegate XNNPACK)
    endif()
  endif()

  target_compile_options(ai_lifeguard PRIVATE -Wall -Wextra -O2)
  install(TARGETS ai_lifeguard RUNTIME DESTINATION bin)
endif()
