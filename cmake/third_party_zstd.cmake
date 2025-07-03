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

# Only build static version of zstd.
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)

FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG f8745da6ff1ad1e7bab384bd1f9d742439278e99 # v1.5.7
  GIT_SHALLOW TRUE
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
  SOURCE_SUBDIR build/cmake
)

FetchContent_MakeAvailable(zstd)
