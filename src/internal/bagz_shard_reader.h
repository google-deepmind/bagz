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

#ifndef BAGZ_SRC_INTERNAL_BAGZ_SHARD_READER_H_
#define BAGZ_SRC_INTERNAL_BAGZ_SHARD_READER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/file/file_system/pread_file.h"

namespace bagz::internal {

// BagzShardReader interprets bytes with the Bagz-format.
// See README.md#bagz-format for details.
//
// A BagzShardReader is either in an engaged or in an empty state. The empty
// state is only meant to support default construction and move-assignment,
// and it is a precondition of all non-special member functions that the
// instance be in an engaged state.
//
class BagzShardReader {
 public:
  // Byte range in records file.
  struct ByteRange {
    uint64_t offset;
    uint64_t length;
  };

  // Constructs a reader with `records` and `limits` sections in distinct files,
  // resulting in an engaged state.
  BagzShardReader(absl_nonnull std::unique_ptr<PReadFile> records,
                  absl_nonnull std::unique_ptr<PReadFile> limits)
      : records_(std::move(records)), limits_(std::move(limits)) {}

  // Creates a reader in an empty state.
  BagzShardReader();

  // Move only. The moved-from object is in an empty state.
  BagzShardReader(BagzShardReader&&) noexcept = default;
  BagzShardReader& operator=(BagzShardReader&&) noexcept = default;

  // Returns the number of records in the shard.
  //
  // Recall that `*this` must not be in the empty state.
  size_t size() const { return limits_->size() / sizeof(uint64_t); }

  // Returns the number of (possibly compressed) bytes in the shard.
  //
  // Recall that `*this` must not be in the empty state.
  size_t num_bytes() const { return records_->size(); }

  // Invokes `callback` with a view of the record at the given index. The
  // `record` parameter is not valid after the callback returns. The index
  // must be less than `size()`.
  //
  // Recall that `*this` must not be in the empty state.
  absl::Status Read(
      size_t index,
      absl::FunctionRef<void(absl::string_view record)> callback) const;

  // Returns the byte range in the records file that contains the record at the
  // given index. This byte range can be used with `ReadFromByteRange` to read
  // the record. The index must be less than `size()`.
  //
  // Recall that `*this` must not be in the empty state.
  absl::StatusOr<ByteRange> ReadByteRange(size_t index) const;

  // Invokes `callback` with a view of the byte range represented by the handle.
  // The `record` parameter is not valid after the callback returns.
  //
  // Recall that `*this` must not be in the empty state.
  absl::Status ReadFromByteRange(
      const ByteRange& handle,
      absl::FunctionRef<void(absl::string_view record)> callback) const;

  // Invokes `callback` consecutively with a view of each record in the range
  // [`index`, `index + count`), stopping early when the callback returns false.
  // The `record` parameter is not valid after the callback returns.
  // The value `result_index + index` is the index of the record.
  //
  // Example:
  //   index = 4, count = 3
  //   callback(0, records[4])
  //   callback(1, records[5])
  //   callback(2, records[6])
  //
  // The `callback` is invoked sequentially and in order in the same thread.
  //
  // Recall that `*this` must not be in the empty state.
  absl::Status ReadRange(
      size_t index, size_t count,
      absl::FunctionRef<bool(size_t result_index, absl::string_view record)>
          callback) const;

  // Advanced API.
  //
  // The following APIs support making fewer reads from the limits file than
  // the records file. This is useful when calculating the optimal amount of
  // readahead when balancing performance and RAM usage.
  //
  // Performs a single read on the limits file in the range
  // [`index * sizeof(uint64_t)`, `(index + count) * sizeof(uint64_t)`) into
  // `limits` if `index + count` is less than `size()`; otherwise an error is
  // returned. A non-empty sub-span of limits can be passed to `ReadFromLimits`
  // to read a subset of records.
  //
  // Recall that `*this` must not be in the empty state.
  absl::Status ReadLimits(size_t index, size_t count,
                          absl::Span<uint64_t> limits) const;

  // [`limits.front()`, `limits.back()`). Then calls `callback` with the record
  // range [`limits[i]`, `limits[i + 1]`) for each `i` in the range
  // [0, `limits.size()`) or until `callback` returns false. Invalid `limits`
  // cause an error to be returned and no records to be read; `limits` are valid
  // if they are monotonically increasing and the last value is less than or
  // equal to the size of the records file.
  //
  // Example:
  //   limits = [10, 25, 30, 40]
  //   callback(0, records[10, 25))
  //   callback(1, records[25, 30))
  //   callback(2, records[30, 40))
  //
  // The `callback` is invoked sequentially and in order in the same thread.
  // See README.md#bagz-format for details.
  //
  // Recall that `*this` must not be in the empty state.
  absl::Status ReadFromLimits(
      absl::Span<const uint64_t> limits,
      absl::FunctionRef<bool(size_t result_index, absl::string_view record)>
          callback) const;

 private:
  // Non-null when `*this` is not in the empty state.
  std::unique_ptr<PReadFile> records_;
  std::unique_ptr<PReadFile> limits_;
};

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_BAGZ_SHARD_READER_H_
