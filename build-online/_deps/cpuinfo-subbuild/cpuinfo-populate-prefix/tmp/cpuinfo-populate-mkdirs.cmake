# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/moses/Lifeguard/build-online/cpuinfo")
  file(MAKE_DIRECTORY "/Users/moses/Lifeguard/build-online/cpuinfo")
endif()
file(MAKE_DIRECTORY
  "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-build"
  "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-subbuild/cpuinfo-populate-prefix"
  "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-subbuild/cpuinfo-populate-prefix/tmp"
  "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-subbuild/cpuinfo-populate-prefix/src/cpuinfo-populate-stamp"
  "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-subbuild/cpuinfo-populate-prefix/src"
  "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-subbuild/cpuinfo-populate-prefix/src/cpuinfo-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-subbuild/cpuinfo-populate-prefix/src/cpuinfo-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/moses/Lifeguard/build-online/_deps/cpuinfo-subbuild/cpuinfo-populate-prefix/src/cpuinfo-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
