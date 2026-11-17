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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_C_FILE_SYSTEM_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_C_FILE_SYSTEM_H_

#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "src/file/file_system/c_file_system_abi.h"
#include "src/file/file_system/file_system.h"

namespace bagz {

// The ABI version of the C filesystem plugin interface. Any external plugin
// (such as `bagz-gcs`) must match this version when registering.
inline constexpr size_t kFileSystemAbiVersion = BAGZ_FILE_SYSTEM_ABI_VERSION;

// PyCapsule name for `BagzCFileSystem*`.
inline constexpr const char* absl_nonnull kBagzCFileSystemCapsuleName =
    BAGZ_C_FILE_SYSTEM_CAPSULE_NAME;

// Exports a non-owning `FileSystem` reference as a `BagzCFileSystem` C ABI
// struct. `file_system` must outlive all calls made through the returned struct
// and any files opened from it.
BagzCFileSystem ToCFileSystem(FileSystem& file_system);

// Exports an owning `FileSystem` as a `BagzCFileSystem` C ABI struct. Ownership
// of `file_system` is transferred to the returned struct and released via
// `DestroyCFileSystem` or when imported by `FromCFileSystem`.
BagzCFileSystem ToCFileSystem(
    absl_nonnull std::unique_ptr<FileSystem> file_system);

// Releases `c_file_system->ctx` via `c_file_system->destroy` if present and
// resets all callback, context, and `destroy` fields (preserving `abi_version`
// and `struct_size`). Safe to call on null, non-owning, or already-consumed
// structs.
void DestroyCFileSystem(BagzCFileSystem* absl_nullable c_file_system);

// Wraps a `BagzCFileSystem` C ABI struct as a C++ `FileSystem`.
// Validates `abi_version == kFileSystemAbiVersion`, `struct_size`, and required
// function pointers before taking ownership of `c_file_system->destroy` (if
// non-null). If validation fails, `c_file_system` is left untouched so its
// existing owner can still clean it up.
absl::StatusOr<absl_nonnull std::unique_ptr<FileSystem>> FromCFileSystem(
    void* absl_nullable raw_c_file_system);

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_C_FILE_SYSTEM_H_
