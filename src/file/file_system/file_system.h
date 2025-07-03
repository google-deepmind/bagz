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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_FILE_SYSTEM_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_FILE_SYSTEM_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/write_file.h"

namespace bagz {

// Custom file-system interface. File-systems are registered with a prefix but
// filenames forwarded to this interface will not include the prefix. Only
// supports operations that are required for Bagz.
// Register new file-systems in file_system/register_file_systems.cc.
// Test with file_system/file_system_test_suite.h.
class FileSystem {
 public:
  virtual ~FileSystem() = default;

  // Opens a file for reading. The error message does not need to include the
  // filename or operation. `options` may be interpreted by the file-system.
  virtual absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>> OpenPRead(
      absl::string_view filename_without_prefix,
      absl::string_view options) const = 0;

  // Opens a file for writing. Errors do not need to include the filename or
  // operation. `offset` is the offset to start writing at. If the file does not
  // exist, it will be created. If the file is size (which is zero for a newly
  // created file) is smaller than `offset`, an error is returned. `options` may
  // be interpreted by the file-system.
  virtual absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
      absl::string_view filename_without_prefix, uint64_t offset,
      absl::string_view options) const = 0;

  // Deletes a file. Errors do not need to include the filename or operation.
  // `options` may be interpreted by the file-system.
  virtual absl::Status Delete(absl::string_view filename_without_prefix,
                              absl::string_view options) const = 0;

  // Opens a set of files for reading. See file_system/shard_spec.h for details
  // on the filespec format.
  virtual absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
  BulkOpenPRead(absl::string_view filespec_without_prefix,
                absl::string_view options) const;
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_FILE_SYSTEM_H_
