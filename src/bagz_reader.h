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

// Reference implementation of Bagz reader for POSIX systems.

#ifndef BAGZ_SRC_BAGZ_READER_H_
#define BAGZ_SRC_BAGZ_READER_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/bagz_options.h"
#include "src/file/file_system/pread_file.h"

namespace bagz {

// BagzReader for reading a collection of Bagz-formatted shards.
// See README.md for details on the format.
class BagzReader {
 public:
  // Opening options.
  struct Options {
    // Specifies specifies how input indexes/ranges are mapped to the underlying
    // records within the shards. See README.md#sharding.
    ShardingLayout sharding_layout = ShardingLayout::kConcatenated;

    // Whether limits are at the end of the file or in a separate file with the
    // same name as the record file but with the prefix "limits.".
    LimitsPlacement limits_placement = LimitsPlacement::kTail;

    // Whether to decompress the records with ZSTD after reading them.
    Compression compression = CompressionAutoDetect{};

    // Whether to read the limits from disk for every read or to cache the
    // limits in memory.
    LimitsStorage limits_storage = LimitsStorage::kOnDisk;

    // Maximum number of parallel operations. Creates an executor with this
    // many threads/fibers.
    int max_parallelism = 100;

    constexpr static size_t kDefaultReadAheadBytes = 1024 * 1024;  // 1 MiB
    // Number of bytes to read ahead when iterating.
    std::optional<size_t> read_ahead_bytes;
  };

  // Handle to a record in a Bagz-formatted file.
  struct Handle {
    size_t shard;
    uint64_t offset;
    uint64_t num_bytes;
  };

  // Opens collection of Bagz-formatted shards using existing PReadFiles.
  //
  // The `options` parameter contains the opening options as described above.
  //
  // Constraints:
  //   `options.compression` must not be `AutoDetect{}`.
  //
  //   If `limits_placement == kSeparate` then `limits_files` must have the same
  //   size as `record_files`, otherwise it must be empty.
  //
  // An error is returned if the files are not in the correct format or if the
  // constraints are not met.
  static absl::StatusOr<BagzReader> OpenFiles(
      absl::Span<absl_nonnull std::unique_ptr<PReadFile>> record_files,
      absl::Span<absl_nonnull std::unique_ptr<PReadFile>> limits_files,
      Options options);

  // Opens a collection of Bagz-formatted files (shards).
  //
  // `filespec` is either:
  //
  //   * filename (e.g. "fs:/path/to/foo.bagz").
  //   * sharded file-spec (e.g. "fs:/path/to/foo@100.bagz").
  //   * comma-separated list of filenames and sharded file-specs
  //     (e.g. "fs:/path/to/f@3.bagz,fs:/path/to/bar.bagz").
  //
  // The `options` parameter contains the opening options as described above.
  //
  // Returns an error if the files fail to open or are not in the correct
  // format.
  static absl::StatusOr<BagzReader> Open(absl::string_view filespec,
                                         Options options);

  // Returns view of the records in the bag.
  absl::StatusOr<BagzReader> Slice(size_t start, int64_t step,
                                   size_t length) const;

  // Returns the opening options.
  const Options& options() const;

  // Returns the number of records in the bag.
  [[nodiscard]] size_t size() const;

  // Returns the approximate number of (possibly compressed) bytes per record in
  // the bag.
  //
  // This is an accurate value when the reader is created. When the reader is
  // sliced this value is not updated and becomes an estimate.
  double ApproximateNumBytesPerRecord() const;

  // Returns the record at the given index.
  absl::StatusOr<std::string> operator[](size_t index) const;

  // Returns all the records in the range [start, start + num_records).
  absl::StatusOr<std::vector<std::string>> ReadRange(size_t start,
                                                     size_t num_records) const;

  // Returns all the records in the the bag.
  absl::StatusOr<std::vector<std::string>> Read() const {
    return ReadRange(0, size());
  }

  // Returns the records at the given indices.
  absl::StatusOr<std::vector<std::string>> ReadIndices(
      absl::Span<const size_t> indices) const;

  // Reads the record at the given index. The callback `allocate` is used to
  // obtain storage for the record: On success, `allocate` is called exactly
  // once and with the size of the decompressed record, and it must return a
  // buffer of exactly that size; the record data is written to that buffer.
  // On failure, `allocate` may or may not be called. If it is called, then it
  // also must return a range of exactly the given size, and the returned
  // buffer is left in an unspecified state.
  absl::Status ReadWithAllocator(
      size_t index,
      absl::FunctionRef<absl::Span<char>(size_t record_size)> allocate) const;

  // On success, and for each `index` in the range, invokes
  // `allocate_for_index(result_index, record_size)` where `result_index`
  // is offset in the range and `record_size` is the size of the uncompressed
  // record at `index`, and it must return a buffer of exactly that
  // `record_size`; the uncompressed record is then copied into the returned
  // buffer.
  //
  // On failure, `allocate_for_index` may be called for some or all of
  // the indices in the range depending on the error and the resulting buffer
  // may be left in an undefined state.
  //
  // WARNING: callbacks are issued in an unspecified order and possibly in
  // parallel from multiple threads. See also `options.max_parallelism`.
  absl::Status ReadRangeWithAllocator(
      size_t start, size_t num_records,
      absl::FunctionRef<absl::Span<char>(size_t result_index,
                                         size_t record_size)>
          allocate_for_index) const;

  // Reads the records in the bag. See `ReadRangeWithAllocator` for details.
  absl::Status ReadWithAllocator(
      absl::FunctionRef<absl::Span<char>(size_t result_index,
                                         size_t record_size)>
          allocate_for_index) const {
    return ReadRangeWithAllocator(0, size(), allocate_for_index);
  }

  // On success, and for each `index` in `indices`, invokes
  // `allocate_for_index(result_index, record_size)` where `result_index`
  // is offset in the range and `record_size` is the size of the uncompressed
  // record at `index`, and it must return a buffer of exactly that
  // `record_size`; the uncompressed record is then copied into the returned
  // buffer.
  //
  // On failure, `allocate_for_index` may be called for some or all of the
  // indices in the range depending on the error and the resulting buffer may be
  // left in an undefined state.
  //
  // WARNING: callbacks are issued in an unspecified order and possibly in
  // parallel from multiple threads. See also `options.max_parallelism`.
  absl::Status ReadIndicesWithAllocator(
      absl::Span<const size_t> indices,
      absl::FunctionRef<absl::Span<char>(size_t result_index,
                                         size_t record_size)>
          allocate_for_index,
      absl::FunctionRef<void(size_t from_index, size_t to_index)> copy_result)
      const;

  // Returns the handle of the record at the given index.
  absl::StatusOr<Handle> ReadHandle(size_t index) const;

  // Returns the record at the given handle.
  absl::StatusOr<std::string> ReadFromHandle(Handle handle) const;

  // Reads the record at the given handle. The callback `allocate` is used to
  // obtain storage for the record: On success, `allocate` is called exactly
  // once and with the size of the decompressed record, and it must return a
  // buffer of exactly that size; the record data is written to that buffer.
  // On failure, `allocate` may or may not be called. If it is called, then it
  // also must return a range of exactly the given size, and the returned
  // buffer is left in an unspecified state.
  absl::Status ReadFromHandleWithAllocator(
      Handle handle,
      absl::FunctionRef<absl::Span<char>(size_t record_size)> allocate) const;

  BagzReader() = default;
  BagzReader(BagzReader&&) = default;
  BagzReader(const BagzReader&) = default;
  BagzReader& operator=(BagzReader&&) = default;

 private:
  struct State;
  BagzReader(std::shared_ptr<const State> state, size_t slice_start,
             int64_t slice_step, size_t slice_length)
      : state_(std::move(state)),
        slice_start_(slice_start),
        slice_step_(slice_step),
        slice_length_(slice_length) {}
  std::shared_ptr<const State> state_;
  size_t slice_start_;
  int64_t slice_step_;
  size_t slice_length_;
};

}  // namespace bagz

#endif  // BAGZ_SRC_BAGZ_READER_H_
