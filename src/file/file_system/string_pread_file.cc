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

#include "src/file/file_system/string_pread_file.h"

#include <algorithm>
#include <cstddef>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace bagz {

absl::Status StringPReadFile::PRead(
    size_t offset, size_t num_bytes,
    absl::FunctionRef<bool(absl::string_view)> callback) const {
  if (num_bytes > content_.size() || offset > content_.size() - num_bytes) {
    return absl::OutOfRangeError("PRead out of range");
  }
  if (chunk_size_ > 0) {
    for (size_t i = 0; i < num_bytes; i += chunk_size_) {
      size_t size = std::min(chunk_size_, num_bytes - i);
      if (!callback(absl::string_view(content_).substr(offset + i, size))) {
        return absl::OkStatus();
      }
    }
  } else {
    callback(absl::string_view(content_).substr(offset, num_bytes));
  }
  return absl::OkStatus();
}

}  // namespace bagz
