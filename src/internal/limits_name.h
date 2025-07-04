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

#ifndef BAGZ_SRC_INTERNAL_LIMITS_NAME_H_
#define BAGZ_SRC_INTERNAL_LIMITS_NAME_H_

#include <string>

#include "absl/strings/string_view.h"

namespace bagz::internal {

// Returns the name of the limits file for the given filename.
// `/path/to/foo.bagz` -> `/path/to/limits.foo.bagz`.
// `foo.bagz` -> `limits.foo.bagz`.
std::string LimitsName(absl::string_view filename);

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_LIMITS_NAME_H_
