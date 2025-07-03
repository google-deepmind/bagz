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

#include "src/file/file_system/file_system.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/shard_spec.h"
#include "src/internal/parallel_do.h"

namespace bagz {

namespace {

constexpr int kMaxParallelism = 100;

}  // namespace

absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
FileSystem::BulkOpenPRead(absl::string_view filespec_without_prefix,
                          absl::string_view options) const {
  std::vector<std::string> filenames = ExpandShardSpec(filespec_without_prefix);
  std::vector<std::unique_ptr<PReadFile>> files(filenames.size());
  if (absl::Status status = internal::ParallelDo(
          filenames.size(),
          [&](size_t file_index) -> absl::Status {
            const std::string& filename = filenames[file_index];
            absl::StatusOr<std::unique_ptr<PReadFile>> file =
                OpenPRead(filename, options);
            if (!file.ok()) {
              return std::move(file).status();
            }

            files[file_index] = *std::move(file);
            return absl::OkStatus();
          },
          /* max_parallelism */ kMaxParallelism, /*cpu_bound=*/false);
      !status.ok()) {
    return status;
  }

  return files;
}

}  // namespace bagz
