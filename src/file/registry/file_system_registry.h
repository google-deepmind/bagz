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

#ifndef BAGZ_SRC_FILE_REGISTRY_FILE_SYSTEM_REGISTRY_H_
#define BAGZ_SRC_FILE_REGISTRY_FILE_SYSTEM_REGISTRY_H_

#include <string>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "src/file/file_system/file_system.h"

namespace bagz {

// Result of resolving a filename_with_prefix to a file system and filename.
struct ResolvedFile {
  // The file system that handles the file.
  FileSystem* absl_nonnull file_system;

  // The filename without the prefix.
  absl::string_view filename;
};

// For registering and unregistering file systems by prefix. Files are handled
// by the file system that is registered for the name by which the file is
// referenced:
//
// * For a name containing a prefix ending with a colon, like "/foo:/a/b/c" or
//   "/bar:x/y/z", the prefix is the initial substring up to and including the
//   colon and skipping a leading slash if present (i.e. "foo:", "bar:" in the
//   examples), and the underlying filename is the remainder of the string.
// * Otherwise, if no colon is present, the prefix is the empty string "", and
//   the underlying filename is the entire string. In this way the empty prefix
//   identifies the default file system which is used for names without an
//   explicit prefix.
//
// In either case it is an error to use a name for which no file system is
// registered.
//
// All methods are thread-safe.
class FileSystemRegistry {
 public:
  // Returns the singleton instance of the registry.
  // Do not call this from RegisterFileSystems. Use passed in registry instead.
  // Thread-safe.
  static FileSystemRegistry& Instance() ABSL_LOCKS_EXCLUDED(mutex_);

  // Returns the ResolvedFile for a given `filename_with_prefix`, if there
  // exists a registered a file system for it, otherwise returns an
  // NotFoundError. Thread-safe.
  absl::StatusOr<ResolvedFile> Resolve(
      absl::string_view filename_with_prefix) const ABSL_LOCKS_EXCLUDED(mutex_);

  // Registers a file system for a given `prefix`, which must be unique and
  // either contain no slashes ('/') and end with a colon (':'), or be an empty
  // string; otherwise an error is returned.
  //
  // `file_system` must not be destroyed until it is either unregistered from
  // the registry or until the registry is destroyed. Callsite is recorded for
  // debugging purposes. It is recommended to call this method from
  // `register_file_systems.cc`.
  //
  // Thread-safe.
  absl::Status Register(absl::string_view prefix, FileSystem& file_system)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Unregisters a file system for a given prefix. Returns an error if the
  // prefix is not registered.
  //
  // Thread-safe.
  absl::Status Unregister(absl::string_view prefix) ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  mutable absl::Mutex mutex_;

  absl::flat_hash_map<std::string, FileSystem* absl_nonnull> file_systems_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_REGISTRY_FILE_SYSTEM_REGISTRY_H_
