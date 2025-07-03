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

// This file contains functions for opening and deleting files.
// A prefix in filenames/file_specs is used to specify the file system.
//
// Examples:
//   * "foo:/path/file.txt" will forward calls to the file system registered as
//     "foo:" and the filename will be "/path/file.txt".
//   * "/path/file.txt" will forward calls to the file system registered as
//     empty string ("") and the filename will be "/path/file.txt".
//
// All file systems should be registered in `register_file_systems.cc` but it
// is possible to register a file system dynamically.

#ifndef BAGZ_SRC_FILE_FILE_H_
#define BAGZ_SRC_FILE_FILE_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/write_file.h"

namespace bagz::file {

// Opens a file for parallel reading.
//
// `options` are passed to the underlying file system.
absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>> OpenPRead(
    absl::string_view filename_with_prefix, absl::string_view options = {});

// Opens a file for writing.  If the file does not exist, it will be created.
// Otherwise the file will be truncated to zero length.
//
// `options` are passed to the underlying file system.
absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
    absl::string_view filename_with_prefix, absl::string_view options = {});

// Opens a file for appending. `offset` is the offset to start writing at. If
// the file does not exist, it will be created. It is an error to open a file
// for writing at an offset that is non-zero for a file that does not exist.
// For an existing file the following will happen:
//
//   * If `offset` is zero, the file will be truncated to zero length.
//   * If `offset` less than the current file size, the file will be truncated
//     to `offset` and the file will be opened for append.
//   * If `offset` is equal to the current file size, the file will be
//     opened for append.
//   * If `offset` is greater than the current file size, an error will be
//     returned.
//
// `options` are passed to the underlying file system.
absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
    absl::string_view filename_with_prefix, uint64_t offset,
    absl::string_view options = {});

// Deletes a file. `options` are passed to the underlying file system.
absl::Status Delete(absl::string_view filename_with_prefix,
                    absl::string_view options = {});

// Expands `file_spec_with_prefix` and opens each file for parallel reading.
// Returns an error if any of the files cannot be opened.
// See file_system/shard_spec.h for details on how file_spec is expanded.
// Comma separated file_specs are allowed to have different prefixes.
//
// `options` are passed to the underlying file system.
absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
BulkOpenPRead(absl::string_view file_spec_with_prefix,
              absl::string_view options = {});

}  // namespace bagz::file

#endif  // BAGZ_SRC_FILE_FILE_H_
