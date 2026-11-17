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

#include "src/file/file_systems/gcs/gcs_file_system.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/shard_spec.h"
#include "src/file/file_system/write_file.h"
#include "src/internal/parallel_do.h"
#include "google/cloud/credentials.h"
#include "google/cloud/internal/rest_options.h"
#include "google/cloud/options.h"
#include "google/cloud/status.h"
#include "google/cloud/status_or.h"
#include "google/cloud/storage/client.h"
#include "google/cloud/storage/download_options.h"
#include "google/cloud/storage/list_objects_reader.h"
#include "google/cloud/storage/object_metadata.h"
#include "google/cloud/storage/object_read_stream.h"
#include "google/cloud/storage/object_write_stream.h"
#include "google/cloud/storage/options.h"
#include "google/cloud/storage/retry_policy.h"
#include "google/cloud/storage/upload_options.h"
#include "google/cloud/storage/well_known_parameters.h"

namespace bagz {
namespace {

constexpr int kMaxParallelism = 100;
constexpr absl::Duration kDefaultRetryTimeout = absl::Seconds(15);
constexpr absl::Duration kDefaultStallTimeout = absl::Seconds(15);
constexpr absl::Duration kAuthStallTimeout = absl::Seconds(5);
constexpr absl::Duration kInitialBackoff = absl::Milliseconds(250);
constexpr absl::Duration kMaxBackoff = absl::Seconds(2);
constexpr double kBackoffScaling = 2.0;

namespace gc = ::google::cloud;
namespace gcs = gc::storage;

gcs::Client MakeDefaultClient() {
  auto options =
      gc::Options{}
          .set<gcs::RetryPolicyOption>(
              gcs::LimitedTimeRetryPolicy(
                  absl::ToChronoSeconds(kDefaultRetryTimeout))
                  .clone())
          .set<gcs::BackoffPolicyOption>(
              gcs::ExponentialBackoffPolicy(
                  absl::ToChronoMilliseconds(kInitialBackoff),
                  absl::ToChronoSeconds(kMaxBackoff), kBackoffScaling)
                  .clone())
          .set<gcs::TransferStallTimeoutOption>(
              absl::ToChronoSeconds(kDefaultStallTimeout))
          .set<gcs::DownloadStallTimeoutOption>(
              absl::ToChronoSeconds(kDefaultStallTimeout));

  if (std::getenv("CLOUD_STORAGE_EMULATOR_ENDPOINT") == nullptr &&
      std::getenv("CLOUD_STORAGE_TESTBENCH_ENDPOINT") == nullptr) {
    auto auth_options =
        gc::Options{}
            .set<gc::rest_internal::DownloadStallTimeoutOption>(
                absl::ToChronoSeconds(kAuthStallTimeout))
            .set<gc::rest_internal::DownloadStallMinimumRateOption>(1)
            .set<gc::rest_internal::TransferStallTimeoutOption>(
                absl::ToChronoSeconds(kAuthStallTimeout))
            .set<gc::rest_internal::TransferStallMinimumRateOption>(1);
    options.set<gc::UnifiedCredentialsOption>(
        gc::MakeGoogleDefaultCredentials(std::move(auth_options)));
  }

  return gcs::Client(std::move(options));
}

// Returns true for auth, credential, or transport errors where discarding the
// cached client allows a subsequent retry (e.g. after authenticating in Colab
// or recovering network access) to re-evaluate credentials and connections.
bool ShouldResetClient(const gc::Status& status) {
  switch (status.code()) {
    case gc::StatusCode::kDeadlineExceeded:
    case gc::StatusCode::kInvalidArgument:
    case gc::StatusCode::kPermissionDenied:
    case gc::StatusCode::kUnauthenticated:
    case gc::StatusCode::kUnavailable:
    case gc::StatusCode::kUnknown:
      return true;
    default:
      return false;
  }
}

}  // namespace

namespace gcs_internal {

class ClientProvider {
 public:
  explicit ClientProvider(GcsFileSystem::ClientFactory client_factory)
      : client_factory_(std::move(client_factory)) {}

  gcs::Client GetClient() ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    if (!client_.has_value()) {
      client_.emplace(client_factory_ != nullptr ? client_factory_()
                                                 : MakeDefaultClient());
    }
    return *client_;
  }

  void ResetClient(const gcs::Client& failed_client)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    if (client_.has_value() && *client_ == failed_client) {
      client_.reset();
    }
  }

 private:
  GcsFileSystem::ClientFactory client_factory_;
  absl::Mutex mutex_;
  std::optional<gcs::Client> client_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace gcs_internal

namespace {

struct BucketObject {
  const std::string bucket;
  const std::string object;
};

absl::Status ConvertStatus(gc::Status status) {
  return absl::Status(static_cast<absl::StatusCode>(status.code()),
                      status.message());
}

// A reference to a GCS object. This reference allows random access to the
// content of the object. Note that changes to the file made in-between reads
// are not checked, which can cause race condition. Only out of range reads are
// checked, which means that if we add data at the end of the file, the reader
// won't be able to access it.
class GcsPReadFile : public PReadFile {
 public:
  explicit GcsPReadFile(
      absl_nonnull std::shared_ptr<gcs_internal::ClientProvider>
          client_provider,
      absl::string_view bucket_name, absl::string_view object_name, size_t size)
      : client_provider_(std::move(client_provider)),
        bucket_name_(bucket_name),
        object_name_(object_name),
        size_(size) {
    CHECK(client_provider_ != nullptr);
  }
  size_t size() const override { return size_; }

  absl::Status PRead(size_t offset,
                     absl::Span<char> destination) const override {
    const size_t num_bytes = destination.size();
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
    if (num_bytes == 0) {
      return absl::OkStatus();
    }

    gcs::Client client = client_provider_->GetClient();
    gcs::ObjectReadStream reader = client.ReadObject(
        bucket_name_, object_name_, gcs::ReadRange(offset, offset + num_bytes));

    reader.read(destination.data(), num_bytes);

    if (!reader.status().ok()) {
      if (ShouldResetClient(reader.status())) {
        client_provider_->ResetClient(client);
      }
      return ConvertStatus(reader.status());
    }
    if (reader.bad() ||
        reader.gcount() != static_cast<std::streamsize>(num_bytes)) {
      return absl::DataLossError(
          absl::StrCat("Failed to read all requested bytes from GCS: expected ",
                       num_bytes, ", got ", reader.gcount()));
    }

    return absl::OkStatus();
  }

  const std::string& Name() const { return object_name_; }

 private:
  const absl_nonnull std::shared_ptr<gcs_internal::ClientProvider>
      client_provider_;
  const std::string bucket_name_;
  const std::string object_name_;
  const size_t size_;
};

class GcsWriteFile : public WriteFile {
 public:
  explicit GcsWriteFile(
      absl_nonnull std::shared_ptr<gcs_internal::ClientProvider>
          client_provider,
      gcs::Client client, gcs::ObjectWriteStream&& writer)
      : client_provider_(std::move(client_provider)),
        client_(std::move(client)),
        writer_(std::move(writer)) {}

  absl::Status Write(absl::string_view data) override {
    writer_.write(data.data(), data.size());
    return HandleStatus(writer_.last_status());
  }
  absl::Status Flush() override {
    writer_.flush();
    return HandleStatus(writer_.last_status());
  }
  absl::Status Close() override {
    writer_.Close();
    return HandleStatus(writer_.last_status());
  }

 private:
  absl::Status HandleStatus(const gc::Status& status) {
    if (ShouldResetClient(status)) {
      client_provider_->ResetClient(client_);
    }
    return ConvertStatus(status);
  }

  absl_nonnull std::shared_ptr<gcs_internal::ClientProvider> client_provider_;
  gcs::Client client_;
  gcs::ObjectWriteStream writer_;
};

// Get the bucket and object name from a GCS URI. Leading `/` are stripped, but
// the URI is expected not to contain the `gs:` prefix. This prefix is already
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

GcsFileSystem::GcsFileSystem(ClientFactory client_factory)
    : client_provider_(std::make_shared<gcs_internal::ClientProvider>(
          std::move(client_factory))) {}

absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>>
GcsFileSystem::OpenWrite(absl::string_view filename_without_prefix,
                         uint64_t offset, absl::string_view options) const {
  const BucketObject bucket_object =
      BucketAndObjectName(filename_without_prefix);
  gcs::Client client = client_provider_->GetClient();

  gcs::ObjectWriteStream writer =
      client.WriteObject(bucket_object.bucket, bucket_object.object,
                         gc::storage::UploadFromOffset(offset));

  if (!writer.last_status().ok()) {
    if (ShouldResetClient(writer.last_status())) {
      client_provider_->ResetClient(client);
    }
    return ConvertStatus(writer.last_status());
  }

  return std::make_unique<GcsWriteFile>(client_provider_, std::move(client),
                                        std::move(writer));
}

absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>>
GcsFileSystem::OpenPRead(absl::string_view filename_without_prefix,
                         absl::string_view options) const {
  const BucketObject bucket_object =
      BucketAndObjectName(filename_without_prefix);
  gcs::Client client = client_provider_->GetClient();

  gc::StatusOr<gcs::ObjectMetadata> metadata =
      client.GetObjectMetadata(bucket_object.bucket, bucket_object.object);

  if (!metadata.ok()) {
    if (ShouldResetClient(metadata.status())) {
      client_provider_->ResetClient(client);
    }
    return ConvertStatus(metadata.status());
  }

  return std::make_unique<GcsPReadFile>(client_provider_, bucket_object.bucket,
                                        bucket_object.object, metadata->size());
}

absl::Status GcsFileSystem::Delete(absl::string_view filename_without_prefix,
                                   absl::string_view options) const {
  const BucketObject bucket_object =
      BucketAndObjectName(filename_without_prefix);
  gcs::Client client = client_provider_->GetClient();
  gc::Status status =
      client.DeleteObject(bucket_object.bucket, bucket_object.object);
  if (ShouldResetClient(status)) {
    client_provider_->ResetClient(client);
  }
  return ConvertStatus(std::move(status));
}

absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
GcsFileSystem::BulkOpenPRead(absl::string_view filespec_without_prefix,
                             absl::string_view options) const {
  std::vector<std::string> expanded_filespec =
      ExpandShardSpec(filespec_without_prefix);

  std::vector<std::vector<std::unique_ptr<GcsPReadFile>>> files_per_shard_spec(
      expanded_filespec.size());
  const gcs::Client shared_client = client_provider_->GetClient();

  if (absl::Status status = internal::ParallelDo(
          expanded_filespec.size(),
          [this, &expanded_filespec, &files_per_shard_spec,
           &shared_client](size_t filespec_index) -> absl::Status {
            gcs::Client client = shared_client;
            absl::string_view filespec = expanded_filespec[filespec_index];
            std::string pattern;
            if (absl::string_view::size_type pos = filespec.rfind("@*");
                pos != absl::string_view::npos) {
              absl::string_view prefix = filespec.substr(0, pos);
              absl::string_view suffix = filespec.substr(pos + 2);
              pattern = absl::StrCat(
                  prefix,
                  "-[0-9][0-9][0-9][0-9][0-9]-of-[0-9][0-9][0-9][0-9][0-9]",
                  suffix);
            } else {
              pattern = filespec;
            }

            const BucketObject bucket_object = BucketAndObjectName(pattern);
            gcs::ListObjectsReader list_objects = client.ListObjects(
                bucket_object.bucket, gcs::MatchGlob(bucket_object.object));

            std::vector<std::unique_ptr<GcsPReadFile>>& files =
                files_per_shard_spec[filespec_index];
            std::vector<std::string> file_names;
            for (gc::StatusOr<gcs::ObjectMetadata> metadata : list_objects) {
              if (!metadata.ok()) {
                if (ShouldResetClient(metadata.status())) {
                  client_provider_->ResetClient(client);
                }
                return ConvertStatus(metadata.status());
              }

              file_names.emplace_back(metadata->name());
              files.push_back(std::make_unique<GcsPReadFile>(
                  client_provider_, metadata->bucket(), metadata->name(),
                  metadata->size()));
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
