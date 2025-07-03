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

#include "src/file/file_system/pread_file.h"

#include <cstddef>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/utility/utility.h"

namespace bagz {

absl::StatusOr<std::string> PReadFile::PReadToString(size_t offset,
                                                     size_t num_bytes) const {
  absl::StatusOr<std::string> result(absl::in_place);
  result->reserve(num_bytes);
  absl::Status status =
      PRead(offset, num_bytes, [&result](absl::string_view piece) {
        result->append(piece);
        return true;
      });
  if (!status.ok()) {
    result = status;
  }
  return result;
}

}  // namespace bagz
