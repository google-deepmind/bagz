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

#include "src/file/registry/file_system_registry.h"

#include <memory>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "src/file/file_system/c_file_system.h"
#include "src/file/file_system/file_system.h"
#include "src/file/registry/register_file_systems.h"

namespace bagz {
namespace {

absl::once_flag file_system_registry_once;

std::pair<absl::string_view, absl::string_view> SplitPrefixAndFilename(
    absl::string_view filename) {
  auto pos = filename.find_first_of(':');
  if (pos == absl::string_view::npos) {
    return {{}, filename};
  }
  absl::string_view prefix = filename.substr(0, pos + 1);
  if (prefix.front() == '/') {
    prefix = prefix.substr(1);
  }
  // Ensure colon is not after an internal slash.
  if (prefix.find_first_of('/') != absl::string_view::npos) {
    return {{}, filename};
  }
  return {prefix, filename.substr(pos + 1)};
}

absl::Status ValidatePrefix(absl::string_view prefix) {
  if (!prefix.empty() && (prefix.find(':') != prefix.size() - 1 ||
                          absl::StrContains(prefix, '/'))) {
    return absl::InvalidArgumentError(absl::StrCat(
        "`prefix` '", prefix,
        "' must empty or end with a ':' and must not contain a '/'."));
  }
  return absl::OkStatus();
}

}  // namespace

FileSystemRegistry& FileSystemRegistry::Instance() {
  static absl::NoDestructor<FileSystemRegistry> instance;
  absl::call_once(file_system_registry_once,
                  []() { RegisterFileSystems(*instance); });
  return *instance;
}

absl::StatusOr<ResolvedFile> FileSystemRegistry::Resolve(
    absl::string_view filename_with_prefix) const {
  const auto& [prefix, filename] = SplitPrefixAndFilename(filename_with_prefix);
  std::shared_ptr<FileSystem> file_system = nullptr;
  {
    absl::MutexLock lock(mutex_);
    if (auto it = file_systems_.find(prefix); it != file_systems_.end()) {
      file_system = it->second;
    }
  }
  if (file_system == nullptr) {
    return absl::NotFoundError(absl::StrCat("No file system registered for '",
                                            filename_with_prefix, "'."));
  }
  return ResolvedFile{.file_system = std::move(file_system),
                      .filename = filename};
}

absl::Status FileSystemRegistry::RegisterLocked(
    absl::string_view prefix,
    absl_nonnull std::shared_ptr<FileSystem> file_system) {
  if (absl::Status status = ValidatePrefix(prefix); !status.ok()) {
    return status;
  }
  if (auto [it, inserted] =
          file_systems_.emplace(prefix, std::move(file_system));
      !inserted) {
    return absl::AlreadyExistsError(absl::StrCat(
        "FileSystem with prefix '", prefix, "' already registered."));
  }
  return absl::OkStatus();
}

absl::Status FileSystemRegistry::Register(absl::string_view prefix,
                                          FileSystem& file_system) {
  absl::MutexLock lock(mutex_);
  return RegisterLocked(
      prefix, std::shared_ptr<FileSystem>(&file_system, [](FileSystem*) {}));
}

absl::Status FileSystemRegistry::Register(
    absl::string_view prefix,
    absl_nonnull std::unique_ptr<FileSystem> file_system) {
  absl::MutexLock lock(mutex_);
  return RegisterLocked(prefix,
                        std::shared_ptr<FileSystem>(std::move(file_system)));
}

absl::Status FileSystemRegistry::Register(
    absl::string_view prefix, void* absl_nullable raw_c_file_system) {
  absl::MutexLock lock(mutex_);
  if (absl::Status status = ValidatePrefix(prefix); !status.ok()) {
    return status;
  }
  if (file_systems_.contains(prefix)) {
    return absl::AlreadyExistsError(absl::StrCat(
        "FileSystem with prefix '", prefix, "' already registered."));
  }
  absl::StatusOr<absl_nonnull std::unique_ptr<FileSystem>> wrapped =
      FromCFileSystem(raw_c_file_system);
  if (!wrapped.ok()) {
    return wrapped.status();
  }
  file_systems_.emplace(prefix,
                        std::shared_ptr<FileSystem>(*std::move(wrapped)));
  return absl::OkStatus();
}

// Unregisters a file system for a given prefix.
absl::Status FileSystemRegistry::Unregister(absl::string_view prefix) {
  std::shared_ptr<FileSystem> release_after_unlock;
  {
    absl::MutexLock lock(mutex_);
    auto node = file_systems_.extract(prefix);
    if (node.empty()) {
      return absl::NotFoundError(
          absl::StrCat("No file system registered for prefix '", prefix, "'."));
    }
    release_after_unlock = std::move(node.mapped());
  }
  return absl::OkStatus();
}

}  // namespace bagz
