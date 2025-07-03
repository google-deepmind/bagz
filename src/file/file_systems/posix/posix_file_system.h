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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEMS_POSIX_POSIX_FILE_SYSTEM_H_
#define BAGZ_SRC_FILE_FILE_SYSTEMS_POSIX_POSIX_FILE_SYSTEM_H_

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/write_file.h"

namespace bagz {

// An implementation of FileSystem in terms of standard C++ and POSIX file
// operations. Note that WriteFile and PReadFile uses POSIX operations.
class PosixFileSystem : public FileSystem {
 protected:
  absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>> OpenPRead(
      absl::string_view filename_without_prefix,
      absl::string_view options) const override;

  absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
      absl::string_view filename_without_prefix, uint64_t offset,
      absl::string_view options) const override;

  absl::Status Delete(absl::string_view filename_without_prefix,
                      absl::string_view options) const override;

  absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
  BulkOpenPRead(absl::string_view filespec_without_prefix,
                absl::string_view options) const override;
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEMS_POSIX_POSIX_FILE_SYSTEM_H_
