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
  GIT_REPOSITORY https://github.com/c-ares/c-ares.git
  GIT_TAG d3a507e920e7af18a5efb7f9f1d8044ed4750013 # v1.34.5
  GIT_SHALLOW TRUE
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG 55f93686c01528224f448c19128836e7df245f72 # v3.12.0
  GIT_SHALLOW TRUE
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  crc32c
  GIT_REPOSITORY https://github.com/google/crc32c.git
  # Version after 1.1.2 to support cmake 4.
  GIT_TAG 2bbb3be42e20a0e6c0f7b39dc07dc863d9ffbc07 # 1.1.2+cmake change.
  GIT_SHALLOW TRUE
  GIT_SUBMODULES_RECURSE
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  re2
  GIT_REPOSITORY https://github.com/google/re2.git
  GIT_TAG 6dcd83d60f7944926bfd308cc13979fc53dd69ca # 2024-05-01
  GIT_SHALLOW TRUE
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  google_cloud_cpp
  GIT_REPOSITORY https://github.com/googleapis/google-cloud-cpp.git
  GIT_TAG c7e213b6d5053f5704fb09b1d4fdcbd71df26a20 # 2.37.0
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
)

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
