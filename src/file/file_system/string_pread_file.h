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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_STRING_PREAD_FILE_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_STRING_PREAD_FILE_H_

#include <cstddef>
#include <string>
#include <utility>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/pread_file.h"

namespace bagz {

// A PReadFile implementation that can be used for testing whether users of
// PReadFile can handle patrial reads correctly.
class StringPReadFile : public PReadFile {
 public:
  // Creates a new PReadFile that will break up reads into chunks of size
  // `chunk_size` if `chunk_size` is greater than 0, otherwise the whole read
  // will be completed in a single call.
  explicit StringPReadFile(std::string content, size_t chunk_size = 0)
      : content_(std::move(content)), chunk_size_(chunk_size) {}

  ~StringPReadFile() override = default;

  size_t size() const override { return content_.size(); };

  absl::Status PRead(
      size_t offset, size_t num_bytes,
      absl::FunctionRef<bool(absl::string_view)> callback) const override;

 private:
  std::string content_;
  size_t chunk_size_;
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_STRING_PREAD_FILE_H_
