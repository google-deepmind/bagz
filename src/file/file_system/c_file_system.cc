// Copyright 2026 Google LLC
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

#include "src/file/file_system/c_file_system.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#include <exception>
#include <new>
#endif

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/file/file_system/c_file_system_abi.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/write_file.h"

namespace bagz {
namespace {

static_assert(std::is_standard_layout_v<BagzCFileSystem>);
static_assert(offsetof(BagzCFileSystem, abi_version) == 0);
static_assert(sizeof(BagzCError) == 4 * sizeof(void*));
static_assert(sizeof(BagzCPReadFileVTable) == 4 * sizeof(void*));
static_assert(sizeof(BagzCPReadFile) == 2 * sizeof(void*));
static_assert(sizeof(BagzCWriteFileVTable) == 5 * sizeof(void*));
static_assert(sizeof(BagzCWriteFile) == 2 * sizeof(void*));
static_assert(sizeof(BagzCFileSystem) == 8 * sizeof(void*));

const char* NullableData(absl::string_view sv) noexcept {
  return sv.empty() ? nullptr : sv.data();
}

absl::string_view ToStringView(const char* data, size_t size) noexcept {
  if (data == nullptr || size == 0) {
    return absl::string_view();
  }
  return absl::string_view(data, size);
}

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
struct StaticString {
  const char* data;
  std::size_t size;

  template <std::size_t N>
  consteval StaticString(const char (&str)[N]) : data(str), size(N - 1) {
    static_assert(N > 0);
  }
};

void PopulateStaticCError(StaticString message,
                          BagzCError* out_error) noexcept {
  if (out_error == nullptr) {
    return;
  }
  *out_error = BagzCError{
      .message = message.data,
      .message_size = message.size,
      .release_ctx = nullptr,
      .release = nullptr,
  };
}
#endif

extern "C" void ReleaseStdStringC(void* ctx) noexcept {
  delete static_cast<std::string*>(ctx);
}

int32_t ToCResult(const absl::Status& status, BagzCError* out_error) noexcept {
  if (status.ok()) {
    return 0;
  }
  const int32_t code = static_cast<int32_t>(status.code());
  if (out_error == nullptr) {
    return code;
  }
  if (status.message().empty()) {
    *out_error = BagzCError{};
    return code;
  }
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
#endif
    auto* message = new std::string(status.message());
    *out_error = BagzCError{
        .message = message->data(),
        .message_size = message->size(),
        .release_ctx = message,
        .release = &ReleaseStdStringC,
    };
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  } catch (...) {
    PopulateStaticCError("Failed to allocate error message", out_error);
  }
#endif
  return code;
}

absl::Status FromCResult(int32_t code, const BagzCError& error) {
  absl::Cleanup release_error = [&error] {
    if (error.release != nullptr) {
      error.release(error.release_ctx);
    }
  };
  if (code == 0) {
    return absl::OkStatus();
  }
  return absl::Status(static_cast<absl::StatusCode>(code),
                      ToStringView(error.message, error.message_size));
}

template <typename Fn>
int32_t RunNoexcept([[maybe_unused]] BagzCError* out_error, Fn&& fn) noexcept {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    return std::forward<Fn>(fn)();
  } catch (const std::bad_alloc&) {
    PopulateStaticCError("Allocation failed in filesystem plugin", out_error);
    return static_cast<int32_t>(absl::StatusCode::kResourceExhausted);
  } catch (const std::exception& e) {
    try {
      return ToCResult(absl::InternalError(e.what()), out_error);
    } catch (...) {
      PopulateStaticCError("Unhandled C++ exception in filesystem plugin",
                           out_error);
      return static_cast<int32_t>(absl::StatusCode::kInternal);
    }
  } catch (...) {
    PopulateStaticCError("Unhandled C++ exception in filesystem plugin",
                         out_error);
    return static_cast<int32_t>(absl::StatusCode::kInternal);
  }
#else
  return std::forward<Fn>(fn)();
#endif
}

extern "C" {

size_t ExportPReadSizeC(void* file) noexcept {
  return static_cast<const PReadFile*>(file)->size();
}

int32_t ExportPReadC(void* file, size_t offset, char* destination,
                     size_t num_bytes, BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() {
    char empty_byte = 0;
    char* dst = (num_bytes == 0) ? &empty_byte : destination;
    return ToCResult(static_cast<const PReadFile*>(file)->PRead(
                         offset, absl::MakeSpan(dst, num_bytes)),
                     out_error);
  });
}

void ExportDestroyPReadC(void* file) noexcept {
  delete static_cast<PReadFile*>(file);
}

int32_t ExportWriteC(void* file, const char* data, size_t size,
                     BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() {
    return ToCResult(
        static_cast<WriteFile*>(file)->Write(ToStringView(data, size)),
        out_error);
  });
}

int32_t ExportFlushC(void* file, BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() {
    return ToCResult(static_cast<WriteFile*>(file)->Flush(), out_error);
  });
}

int32_t ExportCloseC(void* file, BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() {
    return ToCResult(static_cast<WriteFile*>(file)->Close(), out_error);
  });
}

void ExportDestroyWriteC(void* file) noexcept {
  delete static_cast<WriteFile*>(file);
}

void ExportDestroyFileSystemC(void* ctx) noexcept {
  delete static_cast<FileSystem*>(ctx);
}

}  // extern "C"

const BagzCPReadFileVTable kPReadFileVTable = {
    .struct_size = sizeof(BagzCPReadFileVTable),
    .size = &ExportPReadSizeC,
    .pread = &ExportPReadC,
    .destroy = &ExportDestroyPReadC,
};

const BagzCWriteFileVTable kWriteFileVTable = {
    .struct_size = sizeof(BagzCWriteFileVTable),
    .write = &ExportWriteC,
    .flush = &ExportFlushC,
    .close = &ExportCloseC,
    .destroy = &ExportDestroyWriteC,
};

extern "C" {

int32_t ExportOpenPReadC(void* ctx, const char* filename, size_t filename_size,
                         const char* options, size_t options_size,
                         BagzCPReadFile* out_file,
                         BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() -> int32_t {
    absl::StatusOr<std::unique_ptr<PReadFile>> file =
        static_cast<const FileSystem*>(ctx)->OpenPRead(
            ToStringView(filename, filename_size),
            ToStringView(options, options_size));
    if (!file.ok()) {
      return ToCResult(file.status(), out_error);
    }
    *out_file = BagzCPReadFile{
        .file = file->release(),
        .vtable = &kPReadFileVTable,
    };
    return 0;
  });
}

int32_t ExportOpenWriteC(void* ctx, const char* filename, size_t filename_size,
                         uint64_t offset, const char* options,
                         size_t options_size, BagzCWriteFile* out_file,
                         BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() -> int32_t {
    absl::StatusOr<std::unique_ptr<WriteFile>> file =
        static_cast<const FileSystem*>(ctx)->OpenWrite(
            ToStringView(filename, filename_size), offset,
            ToStringView(options, options_size));
    if (!file.ok()) {
      return ToCResult(file.status(), out_error);
    }
    *out_file = BagzCWriteFile{
        .file = file->release(),
        .vtable = &kWriteFileVTable,
    };
    return 0;
  });
}

int32_t ExportDeleteFileC(void* ctx, const char* filename, size_t filename_size,
                          const char* options, size_t options_size,
                          BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() {
    return ToCResult(static_cast<const FileSystem*>(ctx)->Delete(
                         ToStringView(filename, filename_size),
                         ToStringView(options, options_size)),
                     out_error);
  });
}

int32_t ExportBulkOpenPReadC(
    void* ctx, const char* filespec, size_t filespec_size, const char* options,
    size_t options_size, void* sink_ctx,
    void (*sink)(void* sink_ctx, const BagzCPReadFile* files, size_t count),
    BagzCError* out_error) noexcept {
  return RunNoexcept(out_error, [&]() -> int32_t {
    absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>> files =
        static_cast<const FileSystem*>(ctx)->BulkOpenPRead(
            ToStringView(filespec, filespec_size),
            ToStringView(options, options_size));
    if (!files.ok()) {
      return ToCResult(files.status(), out_error);
    }
    std::vector<BagzCPReadFile> c_files;
    c_files.reserve(files->size());
    for (std::unique_ptr<PReadFile>& file : *files) {
      c_files.push_back(BagzCPReadFile{
          .file = file.release(),
          .vtable = &kPReadFileVTable,
      });
    }
    sink(sink_ctx, c_files.data(), c_files.size());
    return 0;
  });
}

}  // extern "C"

template <typename CFile>
void DestroyIfPossible(const CFile& c_file) noexcept {
  using VTable =
      std::remove_cv_t<std::remove_pointer_t<decltype(c_file.vtable)>>;
  if (c_file.vtable != nullptr &&
      c_file.vtable->struct_size == sizeof(VTable) &&
      c_file.vtable->destroy != nullptr) {
    c_file.vtable->destroy(c_file.file);
  }
}

bool IsValidCPReadFile(const BagzCPReadFile& c_file) {
  return c_file.vtable != nullptr &&
         c_file.vtable->struct_size == sizeof(BagzCPReadFileVTable) &&
         c_file.vtable->size != nullptr && c_file.vtable->pread != nullptr &&
         c_file.vtable->destroy != nullptr;
}

bool IsValidCWriteFile(const BagzCWriteFile& c_file) {
  return c_file.vtable != nullptr &&
         c_file.vtable->struct_size == sizeof(BagzCWriteFileVTable) &&
         c_file.vtable->write != nullptr && c_file.vtable->flush != nullptr &&
         c_file.vtable->close != nullptr && c_file.vtable->destroy != nullptr;
}

// Resets callbacks, context, and `destroy` while preserving `abi_version` and
// `struct_size` so consumed or destroyed structs cannot double-destroy and fail
// validation with null required function pointers on reuse.
void MarkConsumed(BagzCFileSystem* absl_nonnull c_fs) noexcept {
  *c_fs = BagzCFileSystem{
      .abi_version = c_fs->abi_version,
      .struct_size = c_fs->struct_size,
  };
}

// Shared owner for `BagzCFileSystem` state, retained by both the wrapped
// `FileSystem` and any `PReadFile`/`WriteFile` handles opened from it so
// `c_fs.destroy` never runs while child file handles remain open.
struct SharedCFileSystem {
  explicit SharedCFileSystem(BagzCFileSystem c_fs_in) : c_fs(c_fs_in) {}
  ~SharedCFileSystem() { DestroyCFileSystem(&c_fs); }

  SharedCFileSystem(const SharedCFileSystem&) = delete;
  SharedCFileSystem& operator=(const SharedCFileSystem&) = delete;

  BagzCFileSystem c_fs;
};

class WrappedCPReadFile final : public PReadFile {
 public:
  WrappedCPReadFile(std::shared_ptr<SharedCFileSystem> fs_state,
                    BagzCPReadFile c_file)
      : fs_state_(std::move(fs_state)), c_file_(c_file) {}
  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  ~WrappedCPReadFile() override { c_file_.vtable->destroy(c_file_.file); }

  WrappedCPReadFile(const WrappedCPReadFile&) = delete;
  WrappedCPReadFile& operator=(const WrappedCPReadFile&) = delete;

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  size_t size() const override { return c_file_.vtable->size(c_file_.file); }

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  absl::Status PRead(size_t offset,
                     absl::Span<char> destination) const override {
    char empty_byte = 0;
    char* dst = destination.empty() ? &empty_byte : destination.data();
    BagzCError error{};
    int32_t code = c_file_.vtable->pread(c_file_.file, offset, dst,
                                         destination.size(), &error);
    return FromCResult(code, error);
  }

 private:
  // Declared before `c_file_` so `c_file_` is destroyed before `fs_state_`.
  std::shared_ptr<SharedCFileSystem> fs_state_;
  BagzCPReadFile c_file_;
};

class WrappedCWriteFile final : public WriteFile {
 public:
  WrappedCWriteFile(std::shared_ptr<SharedCFileSystem> fs_state,
                    BagzCWriteFile c_file)
      : fs_state_(std::move(fs_state)), c_file_(c_file) {}
  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  ~WrappedCWriteFile() override { c_file_.vtable->destroy(c_file_.file); }

  WrappedCWriteFile(const WrappedCWriteFile&) = delete;
  WrappedCWriteFile& operator=(const WrappedCWriteFile&) = delete;

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  absl::Status Write(absl::string_view data) override {
    BagzCError error{};
    int32_t code = c_file_.vtable->write(c_file_.file, NullableData(data),
                                         data.size(), &error);
    return FromCResult(code, error);
  }

  absl::Status Flush() override {
    BagzCError error{};
    int32_t code = c_file_.vtable->flush(c_file_.file, &error);
    return FromCResult(code, error);
  }

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  absl::Status Close() override {
    BagzCError error{};
    int32_t code = c_file_.vtable->close(c_file_.file, &error);
    return FromCResult(code, error);
  }

 private:
  std::shared_ptr<SharedCFileSystem> fs_state_;
  BagzCWriteFile c_file_;
};

enum class BulkOpenSinkError {
  kOk,
  kNullFilesArray,
  kFileCountOverflow,
  kInvalidFile,
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  kAllocationFailed,
#endif
};

struct BulkOpenSinkState {
  std::shared_ptr<SharedCFileSystem> fs_state;
  std::vector<absl_nonnull std::unique_ptr<PReadFile>> files;
  BulkOpenSinkError error = BulkOpenSinkError::kOk;
};

extern "C" void ImportBulkOpenSinkC(void* sink_ctx, const BagzCPReadFile* files,
                                    size_t count) noexcept {
  if (sink_ctx == nullptr || count == 0) {
    return;
  }
  auto* sink_state = static_cast<BulkOpenSinkState*>(sink_ctx);
  if (files == nullptr) {
    if (sink_state->error == BulkOpenSinkError::kOk) {
      sink_state->error = BulkOpenSinkError::kNullFilesArray;
    }
    return;
  }
  if (count > sink_state->files.max_size() - sink_state->files.size()) {
    if (sink_state->error == BulkOpenSinkError::kOk) {
      sink_state->error = BulkOpenSinkError::kFileCountOverflow;
    }
    return;
  }

  size_t i = 0;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
#endif
    if (sink_state->error == BulkOpenSinkError::kOk) {
      sink_state->files.reserve(sink_state->files.size() + count);
    }
    while (i < count) {
      const BagzCPReadFile& c_file = files[i];
      if (sink_state->error != BulkOpenSinkError::kOk ||
          !IsValidCPReadFile(c_file)) {
        if (sink_state->error == BulkOpenSinkError::kOk) {
          sink_state->error = BulkOpenSinkError::kInvalidFile;
        }
        ++i;
        DestroyIfPossible(c_file);
        continue;
      }
      auto wrapped =
          std::make_unique<WrappedCPReadFile>(sink_state->fs_state, c_file);
      sink_state->files.push_back(std::move(wrapped));
      ++i;
    }
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  } catch (...) {
    if (sink_state->error == BulkOpenSinkError::kOk) {
      sink_state->error = BulkOpenSinkError::kAllocationFailed;
    }
    for (; i < count; ++i) {
      DestroyIfPossible(files[i]);
    }
  }
#endif
}

class WrappedCFileSystem final : public FileSystem {
 public:
  explicit WrappedCFileSystem(BagzCFileSystem c_fs)
      : state_(std::make_shared<SharedCFileSystem>(c_fs)) {}

  WrappedCFileSystem(const WrappedCFileSystem&) = delete;
  WrappedCFileSystem& operator=(const WrappedCFileSystem&) = delete;

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>> OpenPRead(
      absl::string_view filename_without_prefix,
      absl::string_view options) const override {
    const BagzCFileSystem& c_fs = state_->c_fs;
    BagzCPReadFile c_file{};
    BagzCError error{};
    int32_t code =
        c_fs.open_pread(c_fs.ctx, NullableData(filename_without_prefix),
                        filename_without_prefix.size(), NullableData(options),
                        options.size(), &c_file, &error);
    if (absl::Status status = FromCResult(code, error); !status.ok()) {
      return status;
    }
    if (!IsValidCPReadFile(c_file)) {
      DestroyIfPossible(c_file);
      return absl::InternalError(
          "Filesystem plugin returned an invalid BagzCPReadFile.");
    }
    return std::make_unique<WrappedCPReadFile>(state_, c_file);
  }

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>> OpenWrite(
      absl::string_view filename_without_prefix, uint64_t offset,
      absl::string_view options) const override {
    const BagzCFileSystem& c_fs = state_->c_fs;
    BagzCWriteFile c_file{};
    BagzCError error{};
    int32_t code =
        c_fs.open_write(c_fs.ctx, NullableData(filename_without_prefix),
                        filename_without_prefix.size(), offset,
                        NullableData(options), options.size(), &c_file, &error);
    if (absl::Status status = FromCResult(code, error); !status.ok()) {
      return status;
    }
    if (!IsValidCWriteFile(c_file)) {
      DestroyIfPossible(c_file);
      return absl::InternalError(
          "Filesystem plugin returned an invalid BagzCWriteFile.");
    }
    return std::make_unique<WrappedCWriteFile>(state_, c_file);
  }

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  absl::Status Delete(absl::string_view filename_without_prefix,
                      absl::string_view options) const override {
    const BagzCFileSystem& c_fs = state_->c_fs;
    BagzCError error{};
    int32_t code =
        c_fs.delete_file(c_fs.ctx, NullableData(filename_without_prefix),
                         filename_without_prefix.size(), NullableData(options),
                         options.size(), &error);
    return FromCResult(code, error);
  }

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI
  absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
  BulkOpenPRead(absl::string_view filespec_without_prefix,
                absl::string_view options) const override {
    const BagzCFileSystem& c_fs = state_->c_fs;
    BulkOpenSinkState state{.fs_state = state_};
    BagzCError error{};
    int32_t code = c_fs.bulk_open_pread(
        c_fs.ctx, NullableData(filespec_without_prefix),
        filespec_without_prefix.size(), NullableData(options), options.size(),
        &state, &ImportBulkOpenSinkC, &error);
    if (absl::Status status = FromCResult(code, error); !status.ok()) {
      return status;
    }
    switch (state.error) {
      case BulkOpenSinkError::kOk:
        break;
      case BulkOpenSinkError::kNullFilesArray:
        return absl::InternalError(
            "Filesystem plugin passed null files array with non-zero count in "
            "BulkOpenPRead.");
      case BulkOpenSinkError::kFileCountOverflow:
        return absl::InternalError(
            "Filesystem plugin exceeded maximum file count in BulkOpenPRead.");
      case BulkOpenSinkError::kInvalidFile:
        return absl::InternalError(
            "Filesystem plugin returned an invalid BagzCPReadFile in "
            "BulkOpenPRead.");
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
      case BulkOpenSinkError::kAllocationFailed:
        return absl::ResourceExhaustedError(
            "Allocation failed in BulkOpenPRead sink.");
#endif
    }
    return std::move(state.files);
  }

 private:
  std::shared_ptr<SharedCFileSystem> state_;
};

}  // namespace

BagzCFileSystem ToCFileSystem(FileSystem& file_system) {
  return BagzCFileSystem{
      .abi_version = kFileSystemAbiVersion,
      .struct_size = sizeof(BagzCFileSystem),
      .ctx = &file_system,
      .destroy = nullptr,
      .open_pread = &ExportOpenPReadC,
      .open_write = &ExportOpenWriteC,
      .delete_file = &ExportDeleteFileC,
      .bulk_open_pread = &ExportBulkOpenPReadC,
  };
}

BagzCFileSystem ToCFileSystem(
    absl_nonnull std::unique_ptr<FileSystem> file_system) {
  BagzCFileSystem c_fs = ToCFileSystem(*file_system);
  c_fs.ctx = file_system.release();
  c_fs.destroy = &ExportDestroyFileSystemC;
  return c_fs;
}

ABSL_ATTRIBUTE_NO_SANITIZE_CFI
void DestroyCFileSystem(BagzCFileSystem* absl_nullable c_file_system) {
  if (c_file_system != nullptr && c_file_system->destroy != nullptr) {
    c_file_system->destroy(c_file_system->ctx);
    MarkConsumed(c_file_system);
  }
}

absl::StatusOr<absl_nonnull std::unique_ptr<FileSystem>> FromCFileSystem(
    void* absl_nullable raw_c_file_system) {
  if (raw_c_file_system == nullptr) {
    return absl::InvalidArgumentError("BagzCFileSystem pointer is null.");
  }
  size_t abi_version = 0;
  std::memcpy(&abi_version, raw_c_file_system, sizeof(abi_version));
  if (abi_version != kFileSystemAbiVersion) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Incompatible bagz filesystem ABI: plugin was built with ABI ",
        abi_version, ", but bagz core provides ABI ", kFileSystemAbiVersion,
        ". Please update or rebuild the plugin."));
  }
  if (reinterpret_cast<uintptr_t>(raw_c_file_system) %
          alignof(BagzCFileSystem) !=
      0) {
    return absl::InvalidArgumentError("BagzCFileSystem pointer is misaligned.");
  }
  auto* c_file_system = static_cast<BagzCFileSystem*>(raw_c_file_system);
  if (c_file_system->struct_size != sizeof(BagzCFileSystem)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Incompatible BagzCFileSystem struct_size: expected ",
        sizeof(BagzCFileSystem), ", got ", c_file_system->struct_size, "."));
  }
  if (c_file_system->open_pread == nullptr ||
      c_file_system->open_write == nullptr ||
      c_file_system->delete_file == nullptr ||
      c_file_system->bulk_open_pread == nullptr) {
    return absl::InvalidArgumentError(
        "BagzCFileSystem has null required function pointers.");
  }

  BagzCFileSystem captured = *c_file_system;
  if (c_file_system->destroy != nullptr) {
    MarkConsumed(c_file_system);
  }
  return std::make_unique<WrappedCFileSystem>(captured);
}

}  // namespace bagz
