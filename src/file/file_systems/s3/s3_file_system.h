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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEMS_S3_S3_FILE_SYSTEM_H_
#define BAGZ_SRC_FILE_FILE_SYSTEMS_S3_S3_FILE_SYSTEM_H_

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
#include <aws/s3/S3Client.h>

namespace bagz {

// An implementation of FileSystem to read files from AWS S3.
class S3FileSystem : public FileSystem {
 public:
  using ClientFactory = std::shared_ptr<Aws::S3::S3Client> (*)();
  explicit S3FileSystem(ClientFactory client_factory = nullptr)
      : client_factory_(client_factory) {}

 protected:
  // Open an S3 object for reading.
  // `filename_without_prefix` should be the URI of the object on S3 without
  // the leading `s3:`.
  absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>> OpenPRead(
      absl::string_view filename_without_prefix,
      absl::string_view options) const override;

  // Open an S3 object for writing, starting at a given offset. After opening
  // the file, any data after that offset will be deleted.
  // `filename_without_prefix` should be the URI of the object on S3 without
  // the leading `s3:`.
  absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
      absl::string_view filename_without_prefix, uint64_t offset,
      absl::string_view options) const override;

  // Delete an S3 object.
  // `filename_without_prefix` should be the URI of the object on S3 without
  // the leading `s3:`.
  absl::Status Delete(absl::string_view filename_without_prefix,
                      absl::string_view options) const override;

  // Open a set of files for reading. See file_system/shard_spec.h for details
  // on the filespec format.
  // `filename_without_prefix` should be the URI of the object on S3 without
  // the leading `s3:`.
  absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
  BulkOpenPRead(absl::string_view filespec_without_prefix,
                absl::string_view options) const override;

 private:
  std::shared_ptr<Aws::S3::S3Client> Client() const;
  ClientFactory client_factory_;
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEMS_S3_S3_FILE_SYSTEM_H_