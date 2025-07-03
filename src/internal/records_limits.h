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

#ifndef BAGZ_SRC_INTERNAL_RECORDS_LIMITS_H_
#define BAGZ_SRC_INTERNAL_RECORDS_LIMITS_H_

#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "src/file/file_system/pread_file.h"

namespace bagz::internal {

struct RecordsLimits {
  absl_nonnull std::unique_ptr<PReadFile> records;
  absl_nonnull std::unique_ptr<PReadFile> limits;
};

// Splits bag_content into records and limits. See README.md#file-format.
absl::StatusOr<RecordsLimits> SplitRecordsAndLimits(
    absl_nonnull std::unique_ptr<PReadFile> bag_content);

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_RECORDS_LIMITS_H_
