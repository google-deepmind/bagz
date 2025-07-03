// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/internal/limits_name.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace bagz::internal {

constexpr absl::string_view kLimitsSuffix = "limits.";

std::string LimitsName(absl::string_view filename) {
  auto start_pos = filename.find_last_of('/');
  if (start_pos == absl::string_view::npos) {
    start_pos = 0;
  } else {
    ++start_pos;
  }
  return absl::StrCat(filename.substr(0, start_pos), kLimitsSuffix,
                      filename.substr(start_pos));
}

}  // namespace bagz::internal
