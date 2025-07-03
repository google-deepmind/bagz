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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEMS_GCS_GCS_FILE_SYSTEM_H_
#define BAGZ_SRC_FILE_FILE_SYSTEMS_GCS_GCS_FILE_SYSTEM_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/write_file.h"
#include "google/cloud/storage/client.h"

namespace bagz {

// An implementation of FileSystem to read file in GCS. This uses the tensorflow
// GCS filesystem library.
class GcsFileSystem : public FileSystem {
 public:
  using ClientFactory = google::cloud::storage::Client (*)();
  explicit GcsFileSystem(ClientFactory client_factory = nullptr)
      : client_factory_(client_factory) {}

 protected:
  // Open a GCS object for reading.
  // `filename_without_prefix` should be the URI of the object on GCS without
  // the leading `gs:`.
  absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>> OpenPRead(
      absl::string_view filename_without_prefix,
      absl::string_view options) const override;

  // Open a GCS object for writing, starting at a given offset. After opening
  // the file, any data after that offset will be deleted.
  // `filename_without_prefix` should be the URI of the object on GCS without
  // the leading `gs:`.
  absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
      absl::string_view filename_without_prefix, uint64_t offset,
      absl::string_view options) const override;

  // Delete a GCS object.
  // `filename_without_prefix` should be the URI of the object on GCS without
  // the leading `gs:`.
  absl::Status Delete(absl::string_view filename_without_prefix,
                      absl::string_view options) const override;

  // Open a set of files for reading. See file_system/shard_spec.h for details
  // on the filespec format.
  // `filename_without_prefix` should be the URI of the object on GCS without
  // the leading `gs:`.
  absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
  BulkOpenPRead(absl::string_view filespec_without_prefix,
                absl::string_view options) const override;

 private:
  google::cloud::storage::Client* absl_nonnull Client() const;
  ClientFactory client_factory_;
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEMS_GCS_GCS_FILE_SYSTEM_H_
