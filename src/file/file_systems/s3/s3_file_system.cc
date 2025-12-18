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

#include "src/file/file_systems/s3/s3_file_system.h"

#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/shard_spec.h"
#include "src/file/file_system/write_file.h"
#include "src/internal/parallel_do.h"
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/PutObjectRequest.h>

namespace bagz {
namespace {

constexpr int kMaxParallelism = 100;

struct BucketObject {
  const std::string bucket;
  const std::string object;
};

absl::Status ConvertAwsError(const Aws::Client::AWSError<Aws::S3::S3Errors>& error) {
  // Map AWS error types to absl status codes
  switch (error.GetErrorType()) {
    case Aws::S3::S3Errors::NO_SUCH_KEY:
    case Aws::S3::S3Errors::NO_SUCH_BUCKET:
      return absl::NotFoundError(error.GetMessage());
    case Aws::S3::S3Errors::ACCESS_DENIED:
      return absl::PermissionDeniedError(error.GetMessage());
    case Aws::S3::S3Errors::INVALID_PARAMETER_VALUE:
      return absl::InvalidArgumentError(error.GetMessage());
    default:
      return absl::UnknownError(error.GetMessage());
  }
}

// A reference to an S3 object. This reference allows random access to the
// content of the object. Note that changes to the file made in-between reads
// are not checked, which can cause race condition. Only out of range reads are
// checked, which means that if we add data at the end of the file, the reader
// won't be able to access it.
class S3PReadFile : public PReadFile {
 public:
  explicit S3PReadFile(std::shared_ptr<Aws::S3::S3Client> client,
                       absl::string_view bucket_name,
                       absl::string_view object_name, size_t size)
      : client_(std::move(client)),
        bucket_name_(bucket_name),
        object_name_(object_name),
        size_(size) {
    CHECK(client_ != nullptr);
  }
  size_t size() const override { return size_; }

  absl::Status PRead(
      size_t offset, size_t num_bytes,
      absl::FunctionRef<bool(absl::string_view)> callback) const override {
    if (num_bytes > size()) {
      return absl::OutOfRangeError(
          absl::StrCat("Invalid range: size  > file size (here: ", num_bytes,
                       " > ", size(), ")"));
    }
    if (offset > size() - num_bytes) {
      return absl::OutOfRangeError(absl::StrCat(
          "Invalid range: offset > file size - size (here: ", offset, " > ",
          size() - num_bytes, ")"));
    }

    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(object_name_);

    // Set the range header for partial read
    std::string range = absl::StrCat("bytes=", offset, "-", offset + num_bytes - 1);
    request.SetRange(range);

    auto outcome = client_->GetObject(request);

    if (!outcome.IsSuccess()) {
      return ConvertAwsError(outcome.GetError());
    }

    auto& stream = outcome.GetResult().GetBody();
    std::string contents(std::istreambuf_iterator<char>(stream), {});

    callback(contents);
    return absl::OkStatus();
  }

  const std::string& Name() const { return object_name_; }

 private:
  std::shared_ptr<Aws::S3::S3Client> const client_;
  const std::string bucket_name_;
  const std::string object_name_;
  const size_t size_;
};

class S3WriteFile : public WriteFile {
 public:
  explicit S3WriteFile(std::shared_ptr<Aws::S3::S3Client> client,
                       const std::string& bucket,
                       const std::string& key,
                       uint64_t offset)
      : client_(std::move(client)),
        bucket_(bucket),
        key_(key),
        offset_(offset) {}

  // TODO(yolokfx): this is writing to memory first then flush to s3.
  // Need to have a checker here. Please make sure each of the shard is small enough.
  absl::Status Write(absl::string_view data) override {
    buffer_.append(data.data(), data.size());
    return absl::OkStatus();
  }

  absl::Status Flush() override {
    if (buffer_.empty()) {
      return absl::OkStatus();
    }

    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(bucket_);
    request.SetKey(key_);

    auto stream = std::make_shared<std::stringstream>(buffer_);
    request.SetBody(stream);

    auto outcome = client_->PutObject(request);

    if (!outcome.IsSuccess()) {
      return ConvertAwsError(outcome.GetError());
    }

    buffer_.clear();
    return absl::OkStatus();
  }

  absl::Status Close() override {
    if (!closed_) {
      closed_ = true;
      return Flush();
    }
    return absl::OkStatus();
  }

  ~S3WriteFile() override {
    if (!closed_) {
      (void)Close();
    }
  }

 private:
  std::shared_ptr<Aws::S3::S3Client> const client_;
  const std::string bucket_;
  const std::string key_;
  const uint64_t offset_;
  std::string buffer_;
  bool closed_ = false;
};

// Get the bucket and object name from an S3 URI. Leading `/` are stripped, but
// the URI is expected not to contain the `s3:` prefix. This prefix is already
// stripped by the file registry.
BucketObject BucketAndObjectName(absl::string_view filename) {
  // Remove leading slash.
  while (absl::ConsumePrefix(&filename, "/")) {
  }

  std::pair<absl::string_view, absl::string_view> bucket_and_object_name =
      absl::StrSplit(filename, absl::MaxSplits('/', 1));

  return BucketObject{
      .bucket = std::string(bucket_and_object_name.first),
      .object = std::string(bucket_and_object_name.second),
  };
}

}  // namespace

std::shared_ptr<Aws::S3::S3Client> S3FileSystem::Client() const {
  ClientFactory client_factory = client_factory_;
  if (client_factory == nullptr) {
    client_factory = [] {
      static Aws::SDKOptions options;
      static bool initialized = false;
      if (!initialized) {
        Aws::InitAPI(options);
        initialized = true;
      }
      return std::make_shared<Aws::S3::S3Client>();
    };
  }
  static absl::NoDestructor<std::shared_ptr<Aws::S3::S3Client>> client(
      client_factory());
  return *client;
}

absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>>
S3FileSystem::OpenWrite(absl::string_view filename_without_prefix,
                        uint64_t offset, absl::string_view options) const {
  const BucketObject bucket_object =
      BucketAndObjectName(filename_without_prefix);

  // Note: S3 doesn't support writing at an offset directly like GCS.
  // For simplicity, we'll start a new write. A full implementation would
  // need to handle multipart uploads or read-modify-write for offset support.
  if (offset != 0) {
    return absl::UnimplementedError(
        "S3 write with non-zero offset not yet implemented");
  }

  return std::make_unique<S3WriteFile>(Client(), bucket_object.bucket,
                                       bucket_object.object, offset);
}

absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>>
S3FileSystem::OpenPRead(absl::string_view filename_without_prefix,
                        absl::string_view options) const {
  const BucketObject bucket_object =
      BucketAndObjectName(filename_without_prefix);
  std::shared_ptr<Aws::S3::S3Client> client = Client();

  Aws::S3::Model::HeadObjectRequest request;
  request.SetBucket(bucket_object.bucket);
  request.SetKey(bucket_object.object);

  auto outcome = client->HeadObject(request);

  if (!outcome.IsSuccess()) {
    return ConvertAwsError(outcome.GetError());
  }

  size_t size = outcome.GetResult().GetContentLength();

  return std::make_unique<S3PReadFile>(client, bucket_object.bucket,
                                       bucket_object.object, size);
}

absl::Status S3FileSystem::Delete(absl::string_view filename_without_prefix,
                                  absl::string_view options) const {
  const BucketObject bucket_object =
      BucketAndObjectName(filename_without_prefix);

  Aws::S3::Model::DeleteObjectRequest request;
  request.SetBucket(bucket_object.bucket);
  request.SetKey(bucket_object.object);

  auto outcome = Client()->DeleteObject(request);

  if (!outcome.IsSuccess()) {
    return ConvertAwsError(outcome.GetError());
  }

  return absl::OkStatus();
}

absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
S3FileSystem::BulkOpenPRead(absl::string_view filespec_without_prefix,
                            absl::string_view options) const {
  std::vector<std::string> expanded_filespec =
      ExpandShardSpec(filespec_without_prefix);

  std::vector<std::vector<std::unique_ptr<S3PReadFile>>> files_per_shard_spec(
      expanded_filespec.size());
  std::shared_ptr<Aws::S3::S3Client> client = Client();

  if (absl::Status status = internal::ParallelDo(
          expanded_filespec.size(),
          [&expanded_filespec, &files_per_shard_spec,
           &client](size_t filespec_index) -> absl::Status {
            absl::string_view filespec = expanded_filespec[filespec_index];
            std::string pattern;
            std::string prefix;

            if (absl::string_view::size_type pos = filespec.rfind("@*");
                pos != absl::string_view::npos) {
              absl::string_view prefix_view = filespec.substr(0, pos);
              absl::string_view suffix = filespec.substr(pos + 2);
              pattern = absl::StrCat(
                  prefix_view,
                  "-[0-9][0-9][0-9][0-9][0-9]-of-[0-9][0-9][0-9][0-9][0-9]",
                  suffix);
              prefix = std::string(prefix_view);
            } else {
              pattern = filespec;
              prefix = filespec;
            }

            const BucketObject bucket_object = BucketAndObjectName(prefix);

            Aws::S3::Model::ListObjectsV2Request request;
            request.SetBucket(bucket_object.bucket);
            request.SetPrefix(bucket_object.object);

            std::vector<std::unique_ptr<S3PReadFile>>& files =
                files_per_shard_spec[filespec_index];
            std::vector<std::string> file_names;

            bool done = false;
            while (!done) {
              auto outcome = client->ListObjectsV2(request);

              if (!outcome.IsSuccess()) {
                return ConvertAwsError(outcome.GetError());
              }

              const auto& result = outcome.GetResult();
              for (const auto& object : result.GetContents()) {
                const std::string& key = object.GetKey();
                file_names.emplace_back(key);
                files.push_back(std::make_unique<S3PReadFile>(
                    client, bucket_object.bucket, key, object.GetSize()));
              }

              if (result.GetIsTruncated()) {
                request.SetContinuationToken(result.GetNextContinuationToken());
              } else {
                done = true;
              }
            }

            if (!absl::c_is_sorted(file_names)) {
              return absl::InternalError(
                  "When opening the bagz in bulk, the file names are not "
                  "sorted.");
            }

            return absl::OkStatus();
          },
          kMaxParallelism, /*cpu_bound=*/false);
      !status.ok()) {
    return status;
  }

  size_t total_number_of_files = 0;
  for (const auto& files : files_per_shard_spec) {
    total_number_of_files += files.size();
  }

  std::vector<absl_nonnull std::unique_ptr<PReadFile>> all_files;
  all_files.reserve(total_number_of_files);

  for (auto& files : files_per_shard_spec) {
    all_files.insert(all_files.end(), std::make_move_iterator(files.begin()),
                     std::make_move_iterator(files.end()));
  }

  return all_files;
}

}  // namespace bagz