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
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/shard_spec.h"
#include "src/file/file_system/write_file.h"

namespace bagz {
namespace {

class PosixPReadFile : public PReadFile {
 public:
  PosixPReadFile(int fd, size_t size) : fd_(fd), size_(size) {}
  ~PosixPReadFile() override {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  size_t size() const override { return size_; }

  absl::Status PRead(size_t offset,
                     absl::Span<char> destination) const override {
    size_t num_bytes = destination.size();
    if (num_bytes > size_ || offset > size_ - num_bytes) {
      return absl::OutOfRangeError("Invalid read");
    }
    char* dest = destination.data();
    size_t remaining = num_bytes;
    size_t cur_offset = offset;
    while (remaining > 0) {
      ssize_t bytes_read = pread(fd_, dest, remaining, cur_offset);
      if (bytes_read < 0) {
        if (errno == EINTR) {
          continue;
        }
        return absl::ErrnoToStatus(errno, "pread failed");
      }
      if (bytes_read == 0) {
        return absl::OutOfRangeError("Unexpected EOF");
      }
      dest += bytes_read;
      remaining -= bytes_read;
      cur_offset += bytes_read;
    }
    return absl::OkStatus();
  }

 private:
  int fd_;
  size_t size_;
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
  int fd = open(filename_str.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "open");
  }
  struct stat stat;
  if (fstat(fd, &stat) < 0) {
    close(fd);
    return absl::ErrnoToStatus(errno, "fstat");
  }
  if (S_ISDIR(stat.st_mode)) {
    close(fd);
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot open directory '", filename, "' as file"));
  }
  return std::make_unique<PosixPReadFile>(fd, stat.st_size);
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
