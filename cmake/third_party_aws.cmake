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

# Disable testing.
set(BUILD_TESTING OFF)
set(ENABLE_TESTING OFF)

# Only build S3 client
set(BUILD_ONLY "s3" CACHE STRING "")

# Disable shared libraries
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

FetchContent_Declare(
  aws-sdk-cpp
  GIT_REPOSITORY https://github.com/aws/aws-sdk-cpp.git
  GIT_TAG 1.11.400 # Latest stable version
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(aws-sdk-cpp)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

if (NOT TARGET GTest::gtest)
  FetchContent_MakeAvailable(googletest)
endif()