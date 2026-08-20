# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Only build static version of cares.
set(CARES_STATIC ON CACHE BOOL "")
set(CARES_SHARED OFF CACHE BOOL "")

# Remove support for scan deps, which is only useful when using C++ modules.
unset(CMAKE_CXX_SCANDEP_SOURCE)

# Disable testing.
set(BUILD_TESTING OFF)

FetchContent_Declare(
  c-ares
  URL https://github.com/c-ares/c-ares/releases/download/v1.34.5/c-ares-1.34.5.tar.gz
  URL_HASH SHA256=7d935790e9af081c25c495fd13c2cfcda4792983418e96358ef6e7320ee06346
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  crc32c
  # Version after 1.1.2 to support cmake 4.
  URL https://github.com/google/crc32c/archive/2bbb3be42e20a0e6c0f7b39dc07dc863d9ffbc07.tar.gz
  URL_HASH SHA256=56be8308f23626f82075a035daabd473c8e2b86344768c46182afe86edebf49d
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  re2
  URL https://github.com/google/re2/releases/download/2024-05-01/re2-2024-05-01.tar.gz
  URL_HASH SHA256=fef2f366578401eada34f5603679fb2aebe9b409de8d275a482ce5f2cbac2492
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  google_cloud_cpp
  URL https://github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.37.0.tar.gz
  URL_HASH SHA256=10867580483cb338e7d50920c2383698f3572cc6b4c7d072e38d5f43755cbd80
  EXCLUDE_FROM_ALL
)

# Disable usage of third party libraries for CRC32
set(CRC32C_USE_GLOG OFF)
set(CRC32C_BUILD_TESTS OFF)
set(CRC32C_BUILD_BENCHMARKS OFF)
set(CRC32C_INSTALL OFF)

# Use the absl library provided by this cmake file and disable tests, install
# and shared library.
set(protobuf_BUILD_TESTS OFF)
set(protobuf_ABSL_PROVIDER "package")
set(protobuf_BUILD_SHARED_LIBS OFF)
set(protobuf_INSTALL OFF)

# Only build storage library
set(GOOGLE_CLOUD_CPP_ENABLE "storage" CACHE STRING "")
set(GOOGLE_CLOUD_CPP_WITH_MOCKS OFF CACHE BOOL "")
set(BUILD_TESTING OFF CACHE BOOL "")
# See https://github.com/googleapis/google-cloud-cpp/blob/main/doc/ctype-cord-workarounds.md
set(GOOGLE_CLOUD_CPP_ENABLE_CTYPE_CORD_WORKAROUND OFF CACHE BOOL "")

FetchContent_MakeAvailable(
  c-ares
  crc32c
  nlohmann_json
)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Do not run install commands in google_cloud_cpp. This leads to errors as it
# requires all dependencies to export their files, which causes conflicts in
# the cmake rules.
set(CMAKE_SKIP_INSTALL_RULES ON)

FetchContent_MakeAvailable(re2)

# google_cloud_cpp requires crc32c to be namespaced.

add_library(Crc32c::crc32c ALIAS crc32c)

FetchContent_MakeAvailable(google_cloud_cpp)
