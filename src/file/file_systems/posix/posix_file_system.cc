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

#include "src/file/file_systems/posix/posix_file_system.h"

#include <fcntl.h>
#include <glob.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/shard_spec.h"
#include "src/file/file_system/write_file.h"

namespace bagz {
namespace {

class MmapDeleter {
 public:
  explicit MmapDeleter(size_t mmap_size) : mmap_size_(mmap_size) {}
  void operator()(const void* mmap) const {
    munmap(const_cast<void*>(mmap), mmap_size_);
  }
  size_t mmap_size() const { return mmap_size_; }

 private:
  size_t mmap_size_;
};

using MmapUniquePtr = std::unique_ptr<void, MmapDeleter>;

absl::StatusOr<MmapUniquePtr> MmapFile(const std::string& filename) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "open");
  }
  struct stat stat;
  if (fstat(fd, &stat) < 0) {
    close(fd);
    return absl::ErrnoToStatus(errno, "fstat");
  }

  // We cannot mmap an empty file but we can use an empty string_view.
  if (stat.st_size == 0) {
    close(fd);
    return MmapUniquePtr(nullptr, MmapDeleter(0));
  }

  void* records_mmap =
      mmap(/*addr=*/nullptr, /*length=*/stat.st_size, /*prot=*/PROT_READ,
           /*flags=*/MAP_SHARED, /*fd=*/fd, /*offset=*/0);
  close(fd);
  if (records_mmap == MAP_FAILED) {
    // kNotFound is confusing for user for ENODEV.
    if (errno == ENODEV) {
      return absl::PermissionDeniedError("mmap");
    } else {
      return absl::ErrnoToStatus(errno, "mmap");
    }
  }
  return MmapUniquePtr(records_mmap, MmapDeleter(stat.st_size));
}

class PosixPReadFile : public PReadFile {
 public:
  explicit PosixPReadFile(MmapUniquePtr mmap) : mmap_(std::move(mmap)) {}
  size_t size() const override { return mmap_.get_deleter().mmap_size(); }

  absl::Status PRead(
      size_t offset, size_t num_bytes,
      absl::FunctionRef<bool(absl::string_view)> callback) const override {
    size_t mmap_size = size();
    if (num_bytes > mmap_size || offset > mmap_size - num_bytes) {
      return absl::OutOfRangeError("Invalid read");
    }
    callback(absl::string_view(static_cast<const char*>(mmap_.get()) + offset,
                               num_bytes));
    return absl::OkStatus();
  }

 private:
  MmapUniquePtr mmap_;
};

class PosixWriteFile : public WriteFile {
 public:
  using File =
      std::unique_ptr<std::FILE,
                      decltype([](std::FILE* file) { std::fclose(file); })>;
  explicit PosixWriteFile(File file) : file_(std::move(file)) {}
  absl::Status Write(absl::string_view data) override {
    size_t written = std::fwrite(data.data(), 1, data.size(), file_.get());
    if (written != data.size()) {
      return absl::ErrnoToStatus(errno, "Failed to write to file");
    }
    return absl::OkStatus();
  }
  absl::Status Flush() override {
    if (std::fflush(file_.get()) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to flush file");
    }
    return absl::OkStatus();
  }
  absl::Status Close() override {
    if (file_ == nullptr) {
      return absl::OkStatus();
    }
    absl::Status status = absl::OkStatus();
    if (std::fclose(file_.release()) != 0) {
      status = absl::ErrnoToStatus(errno, "Failed to close file");
    }
    return status;
  }

 private:
  File file_;
};

}  // namespace

absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>>
PosixFileSystem::OpenWrite(absl::string_view filename, uint64_t offset,
                           absl::string_view options) const {
  std::string filename_str(filename);
  PosixWriteFile::File file;
  if (offset > 0) {
    file.reset(std::fopen(filename_str.c_str(), "ab"));
    if (file == nullptr) {
      return absl::ErrnoToStatus(errno, "Failed to open file");
    }
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to seek to end of file");
    }
    if (size_t file_size = std::ftell(file.get()); file_size < offset) {
      return absl::OutOfRangeError(
          absl::StrCat("Invalid offset: ", offset, " > ", file_size));
    }
    if (ftruncate(fileno(file.get()), offset) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to truncate file");
    }
    if (std::fseek(file.get(), offset, SEEK_SET) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to seek file");
    }
  } else {
    file.reset(std::fopen(filename_str.c_str(), "wb"));
    if (file == nullptr) {
      return absl::ErrnoToStatus(errno, "Failed to open file");
    }
  }
  return std::make_unique<PosixWriteFile>(std::move(file));
}

absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>>
PosixFileSystem::OpenPRead(absl::string_view filename,
                           absl::string_view options) const {
  std::string filename_str(filename);
  absl::StatusOr<MmapUniquePtr> mmap = MmapFile(filename_str.c_str());
  if (!mmap.ok()) {
    return mmap.status();
  }
  return std::make_unique<PosixPReadFile>(*std::move(mmap));
}

absl::Status PosixFileSystem::Delete(absl::string_view filename,
                                     absl::string_view options) const {
  std::string filename_str(filename);
  if (std::remove(filename_str.c_str()) != 0) {
    return absl::ErrnoToStatus(errno, "Failed to delete file");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
PosixFileSystem::BulkOpenPRead(absl::string_view filespec_without_prefix,
                               absl::string_view options) const {
  std::string filespec = CanonicaliseShardSpec(
      filespec_without_prefix, [](const std::string& pattern) {
        glob_t glob_result;
        absl::Cleanup cleanup = [&glob_result] { globfree(&glob_result); };
        int return_value =
            glob(pattern.c_str(), GLOB_NOESCAPE, nullptr, &glob_result);
        if (return_value != 0 || glob_result.gl_pathc == 0) {
          return std::string{};
        }
        return std::string(glob_result.gl_pathv[glob_result.gl_pathc - 1]);
      });

  return FileSystem::BulkOpenPRead(filespec, options);
}

}  // namespace bagz
