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

# Libraries depending on absl will use the same compilation flags as absl.
set(ABSL_PROPAGATE_CXX_STD ON)

FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp
  GIT_TAG 76bb24329e8bf5f39704eb10d21b9a80befa7c81 # lts_2025_05_12
  OVERRIDE_FIND_PACKAGE
  EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(absl)
