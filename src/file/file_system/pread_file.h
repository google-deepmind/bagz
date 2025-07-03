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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_PREAD_FILE_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_PREAD_FILE_H_

#include <cstddef>
#include <string>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace bagz {

// Provides a thread-safe interface to read a range of bytes from a file.
// Reading can happen concurrently, repeatedly, and partially, so the exposed
// interface is that of a "multi-pass range" (and not, say, a "stream").
class PReadFile {
 public:
  virtual ~PReadFile() = default;

  // Returns the size of the file in bytes.
  virtual size_t size() const = 0;

  // Reads `num_bytes` starting at `offset`. Returns OutOfRangeError if
  // `offset + num_bytes` is greater than `size()`.
  //
  // The result is split into non-empty pieces and `callback` is invoked
  // sequentially (but not necessarily from the same thread) for each non-empty
  // piece until the total num_bytes is reached or `callback` returns false. If
  // `num_bytes` is zero, `callback` is not invoked. The reader may chose not to
  // split the result into pieces and will invoke `callback` only once.
  //
  // PRead will return an `OkStatus` if either `callback` returns `false`, or
  // `callback` was called with pieces whose sizes accumulate to `num_bytes`.
  // Otherwise an error is returned. The accumulated size of the pieces is
  // guaranteed to be less than or equal to `num_bytes`.
  //
  // Implementation must be thread-safe and support concurrent invocations with
  // arbitrary arguments.
  virtual absl::Status PRead(
      size_t offset, size_t num_bytes,
      absl::FunctionRef<bool(absl::string_view piece)> callback) const = 0;

  // Helper functions.

  // Reads `num_bytes` starting at `offset` and returns the result as a string.
  // Returns OutOfRangeError if `offset + num_bytes` is greater than `size()`.
  //
  // Thread-safe.
  absl::StatusOr<std::string> PReadToString(size_t offset,
                                            size_t num_bytes) const;
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_PREAD_FILE_H_
