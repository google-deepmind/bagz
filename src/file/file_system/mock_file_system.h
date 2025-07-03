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


// Mock file system for testing.

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_MOCK_FILE_SYSTEM_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_MOCK_FILE_SYSTEM_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "testing/base/public/gmock.h"
#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/string_pread_file.h"
#include "src/file/file_system/write_file.h"
#include "src/file/registry/file_system_registry.h"

namespace bagz {

// Mock PReadFile that allows to inject errors into the PRead method.
class MockPReadFile : public StringPReadFile {
 public:
  // Creates a mock file with the given content and reads with given chunk size.
  // See `StringPReadFile` for the details of the chunk size.
  explicit MockPReadFile(std::string content, int chunck_size = 0)
      : StringPReadFile(std::move(content)) {}

  ~MockPReadFile() override = default;

  // Implementation of the `PRead` that calls the `PReadMock` and then the
  // `StringPReadFile::PRead`.
  absl::Status PRead(
      size_t offset, size_t num_bytes,
      absl::FunctionRef<bool(absl::string_view)> callback) const override {
    if (absl::Status status = PReadMock(offset, num_bytes); !status.ok()) {
      return status;
    }
    return StringPReadFile::PRead(offset, num_bytes, callback);
  }

  // Mock method for `PRead` that allows to injecting errors.
  MOCK_METHOD(absl::Status, PReadMock, (size_t offset, size_t num_bytes),
              (const));

 private:
  std::string content_;
};

// Mock WriteFile that allows to inject errors into the different methods.
// The content of the file is stored in the `content()` and `flushed_content()`.
class MockWriteFile : public WriteFile {
 public:
  MockWriteFile() = default;
  ~MockWriteFile() override = default;

  // Implementation of the `Write` that calls the `WriteMock` and then appends
  // the data to `content_`.
  absl::Status Write(absl::string_view data) override {
    if (absl::Status status = WriteMock(); !status.ok()) {
      return status;
    }
    content_.append(data);
    return absl::OkStatus();
  }

  // Implementation of the `Flush` that calls the `FlushMock` and then sets
  // the flushed_content() to the current content.
  absl::Status Flush() override {
    if (absl::Status status = FlushMock(); !status.ok()) {
      return status;
    }
    flushed_size_ = content_.size();
    return absl::OkStatus();
  }

  // Implementation of the `Close` that calls the `CloseMock` and then sets
  // the flushed_content() to the current content.
  absl::Status Close() override {
    if (absl::Status status = CloseMock(); !status.ok()) {
      return status;
    }
    flushed_size_ = content_.size();
    return absl::OkStatus();
  }

  MOCK_METHOD(absl::Status, WriteMock, (), (const));
  MOCK_METHOD(absl::Status, CloseMock, (), (const));
  MOCK_METHOD(absl::Status, FlushMock, (), (const));

  // Returns the content of the file that was flushed. Returned view is
  // invalid after the next call to `Write`.
  absl::string_view flushed_content() const {
    return absl::string_view(content_.data(), flushed_size_);
  }

  // Returns the content of the file including the content that was not flushed
  // yet.
  absl::string_view content() const { return content_; }

 private:
  std::string content_;
  size_t flushed_size_;
};

// Mock file system that is registered in the `FileSystemRegistry` with the
// prefix "mock_fs:".
class MockFileSystem : public FileSystem {
 public:
  MockFileSystem() {
    FileSystemRegistry::Instance().Register("mock_fs:", *this).IgnoreError();
  }
  ~MockFileSystem() override {
    FileSystemRegistry::Instance().Unregister("mock_fs:").IgnoreError();
  }

  // Mock method for `OpenPRead`.
  //
  // Example usage:
  //
  // StrictMock<MockFileSystem> mock_file_system;
  // auto mock_file = std::make_unique<StrictMock<MockPReadFile>>("content");
  // MockPReadFile& mock = *mock_file;
  // EXPECT_CALL(mock_file_system, OpenPRead("file.txt"))
  //     .WillOnce(Return(std::move(mock_file)));
  //
  // file::OpenPRead("mock_fs:file.txt");  // Returns the mock file.
  MOCK_METHOD(absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>>,
              OpenPRead,
              (absl::string_view filename_without_prefix,
               absl::string_view options),
              (const, override));

  // Mock method for `OpenWrite`.
  //
  // Example usage:
  //
  // StrictMock<MockFileSystem> mock_file_system;
  // auto mock_file = std::make_unique<StrictMock<MockWriteFile>>();
  // MockWriteFile& mock = *mock_file;
  // EXPECT_CALL(mock_file_system, OpenWrite("file.txt"))
  //     .WillOnce(Return(std::move(mock_file)));
  //
  // file::OpenWrite("mock_fs:file.txt");  // Returns the mock file.
  MOCK_METHOD(absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>>,
              OpenWrite,
              (absl::string_view filename_without_prefix, uint64_t offset,
               absl::string_view options),
              (const, override));

  MOCK_METHOD(absl::Status, Delete,
              (absl::string_view filename_without_prefix,
               absl::string_view options),
              (const, override));

  MOCK_METHOD(
      absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>,
      BulkOpenPRead,
      (absl::string_view filespec_without_prefix, absl::string_view options),
      (const, override));
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_MOCK_FILE_SYSTEM_H_
