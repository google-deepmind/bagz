// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS"BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/file/file.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/write_file.h"
#include "src/file/registry/file_system_registry.h"

namespace bagz::file {
namespace {

absl::Status Error(const absl::Status& status, absl::string_view operation,
                   absl::string_view filename_with_prefix) {
  return absl::Status(status.code(),
                      absl::StrCat(status.message(), "; ", operation, "(\"",
                                   filename_with_prefix, "\")"));
}

}  // namespace

absl::StatusOr<std::unique_ptr<PReadFile>> OpenPRead(
    absl::string_view filename_with_prefix, absl::string_view options) {
  absl::StatusOr<ResolvedFile> resolved_file =
      FileSystemRegistry::Instance().Resolve(filename_with_prefix);
  if (!resolved_file.ok()) {
    return std::move(resolved_file).status();
  }

  if (absl::StatusOr<std::unique_ptr<PReadFile>> result =
          resolved_file->file_system->OpenPRead(resolved_file->filename,
                                                options);
      result.ok()) {
    return result;
  } else {
    return Error(result.status(), "OpenPRead", filename_with_prefix);
  }
}

// Opens a file for writing.
absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
    absl::string_view filename_with_prefix, absl::string_view options) {
  return OpenWrite(filename_with_prefix, 0, options);
}

absl::StatusOr<std::unique_ptr<WriteFile>> OpenWrite(
    absl::string_view filename_with_prefix, uint64_t offset,
    absl::string_view options) {
  absl::StatusOr<ResolvedFile> resolved_file =
      FileSystemRegistry::Instance().Resolve(filename_with_prefix);
  if (!resolved_file.ok()) {
    return std::move(resolved_file).status();
  }

  if (absl::StatusOr<std::unique_ptr<WriteFile>> result =
          resolved_file->file_system->OpenWrite(resolved_file->filename, offset,
                                                options);
      result.ok()) {
    return result;
  } else {
    return Error(result.status(), "OpenWrite", filename_with_prefix);
  }
}

absl::Status Delete(absl::string_view filename_with_prefix,
                    absl::string_view options) {
  absl::StatusOr<ResolvedFile> resolved_file =
      FileSystemRegistry::Instance().Resolve(filename_with_prefix);
  if (!resolved_file.ok()) {
    return std::move(resolved_file).status();
  }

  if (absl::Status status =
          resolved_file->file_system->Delete(resolved_file->filename, options);
      status.ok()) {
    return status;
  } else {
    return Error(status, "Delete", filename_with_prefix);
  }
}

absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
BulkOpenPRead(absl::string_view file_spec_with_prefix,
              absl::string_view options) {
  // Group files by prefix. Only groups files with contiguous prefixes. To
  // save book keeping.
  std::vector<std::pair<FileSystem*, std::vector<absl::string_view>>>
      filenames_from_prefix;
  FileSystem* last_file_system = nullptr;
  const FileSystemRegistry& registry = FileSystemRegistry::Instance();
  for (const auto& file_spec :
       absl::StrSplit(file_spec_with_prefix, ',', absl::SkipEmpty())) {
    absl::StatusOr<ResolvedFile> resolved_file = registry.Resolve(file_spec);
    if (!resolved_file.ok()) {
      return std::move(resolved_file).status();
    }
    if (resolved_file->file_system != last_file_system) {
      last_file_system = resolved_file->file_system;
      filenames_from_prefix.push_back({last_file_system, {}});
    }
    filenames_from_prefix.back().second.push_back(resolved_file->filename);
  }

  std::vector<std::unique_ptr<PReadFile>> result;
  for (const auto& [file_system, filenames] : filenames_from_prefix) {
    std::string file_spec = absl::StrJoin(filenames, ",");
    absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>> files =
        file_system->BulkOpenPRead(file_spec, options);
    if (!files.ok()) {
      return Error(files.status(), "BulkOpenPRead", file_spec_with_prefix);
    }
    if (result.empty()) {
      result = std::move(*files);
    } else {
      result.insert(result.end(), std::make_move_iterator(files->begin()),
                    std::make_move_iterator(files->end()));
    }
  }
  return result;
}

}  // namespace bagz::file
