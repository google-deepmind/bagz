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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_C_FILE_SYSTEM_ABI_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_C_FILE_SYSTEM_ABI_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI version for the C filesystem plugin interface.
#define BAGZ_FILE_SYSTEM_ABI_VERSION 2

// PyCapsule name used when passing a `BagzCFileSystem*` across Python modules.
#define BAGZ_C_FILE_SYSTEM_CAPSULE_NAME "bagz.BagzCFileSystem"

// Error details populated when a C ABI function returns a non-zero status code
// (matching canonical `absl::StatusCode` integer values; `0` indicates OK).
// On success (`0`), `out_error` is untouched.
typedef struct BagzCError {
  const char* message;
  size_t message_size;
  void* release_ctx;
  // If non-NULL, the caller must invoke `release(release_ctx)` once after
  // reading `message`.
  void (*release)(void* release_ctx);
} BagzCError;

typedef struct BagzCPReadFileVTable {
  size_t struct_size;
  size_t (*size)(void* file);
  int32_t (*pread)(void* file, size_t offset, char* destination,
                   size_t num_bytes, BagzCError* out_error);
  void (*destroy)(void* file);
} BagzCPReadFileVTable;

typedef struct BagzCPReadFile {
  void* file;
  const BagzCPReadFileVTable* vtable;
} BagzCPReadFile;

typedef struct BagzCWriteFileVTable {
  size_t struct_size;
  int32_t (*write)(void* file, const char* data, size_t size,
                   BagzCError* out_error);
  int32_t (*flush)(void* file, BagzCError* out_error);
  int32_t (*close)(void* file, BagzCError* out_error);
  void (*destroy)(void* file);
} BagzCWriteFileVTable;

typedef struct BagzCWriteFile {
  void* file;
  const BagzCWriteFileVTable* vtable;
} BagzCWriteFile;

// C ABI vtable representing a `bagz::FileSystem` implementation.
// All function pointer arguments and return values are scalars or pointers to
// avoid cross-compiler struct-by-value calling-convention differences.
typedef struct BagzCFileSystem {
  // Must equal `BAGZ_FILE_SYSTEM_ABI_VERSION`. Kept as the first field so
  // version mismatches can be diagnosed even if later fields change.
  size_t abi_version;
  // Must equal `sizeof(BagzCFileSystem)`.
  size_t struct_size;

  void* ctx;
  // Optional; if non-NULL, called once when the wrapped `FileSystem` is
  // destroyed.
  void (*destroy)(void* ctx);

  int32_t (*open_pread)(void* ctx, const char* filename, size_t filename_size,
                        const char* options, size_t options_size,
                        BagzCPReadFile* out_file, BagzCError* out_error);

  int32_t (*open_write)(void* ctx, const char* filename, size_t filename_size,
                        uint64_t offset, const char* options,
                        size_t options_size, BagzCWriteFile* out_file,
                        BagzCError* out_error);

  int32_t (*delete_file)(void* ctx, const char* filename, size_t filename_size,
                         const char* options, size_t options_size,
                         BagzCError* out_error);

  // Transfers ownership of all opened `BagzCPReadFile` handles to `sink`.
  int32_t (*bulk_open_pread)(
      void* ctx, const char* filespec, size_t filespec_size,
      const char* options, size_t options_size, void* sink_ctx,
      void (*sink)(void* sink_ctx, const BagzCPReadFile* files, size_t count),
      BagzCError* out_error);
} BagzCFileSystem;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_C_FILE_SYSTEM_ABI_H_
