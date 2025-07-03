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

#include "src/bagz_reader.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/call_once.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/bagz_options.h"
#include "src/file/file.h"
#include "src/file/file_system/pread_file.h"
#include "src/internal/bagz_shard_reader.h"
#include "src/internal/limits_name.h"
#include "src/internal/parallel_do.h"
#include "src/internal/records_limits.h"
#include "src/internal/recycling_pool.h"
#include "src/internal/shard_indexing.h"
#include "src/internal/zstd_decompressor.h"
#include "zstd.h"

namespace bagz {
namespace {

// Reads the entire file into memory on first use the provided PReadFile
// interface.
class InMemoryPReadFile : public PReadFile {
 public:
  explicit InMemoryPReadFile(std::unique_ptr<PReadFile> pread_file)
      : pread_file_(std::move(pread_file)), data_size_(pread_file_->size()) {}

  void InitOnce() {
    data_ = std::make_unique_for_overwrite<char[]>(data_size_);
    char* data = data_.get();
    status_ =
        pread_file_->PRead(0, data_size_, [&data](absl::string_view piece) {
          std::memcpy(data, piece.data(), piece.size());
          data += piece.size();
          return true;
        });
    pread_file_.reset();
    if (!status_.ok()) {
      data_ = nullptr;
    }
  }

  size_t size() const override { return data_size_; }

  absl::Status PRead(size_t offset, size_t num_bytes,
                     absl::FunctionRef<bool(absl::string_view piece)> callback)
      const override {
    absl::call_once(once_, &InMemoryPReadFile::InitOnce,
                    const_cast<InMemoryPReadFile*>(this));
    if (!status_.ok()) {
      return status_;
    }
    callback(
        absl::string_view(data_.get(), data_size_).substr(offset, num_bytes));
    return absl::OkStatus();
  }

 private:
  std::unique_ptr<PReadFile> pread_file_;
  std::unique_ptr<char[]> data_;
  size_t data_size_;
  absl::Status status_;
  mutable absl::once_flag once_;
};

}  // namespace

struct BagzReader::State {
  State(BagzReader::Options options,
        std::vector<internal::BagzShardReader> shards,
        std::vector<size_t> accumulated_count)
      : options_(std::move(options)),
        shards_(std::move(shards)),
        shard_indexing_(
            std::move(accumulated_count),
            options_.sharding_layout == ShardingLayout::kInterleaved) {
    if (std::holds_alternative<CompressionZstd>(options_.compression)) {
      absl::string_view dictionary =
          std::get<CompressionZstd>(options_.compression).dictionary;
      decompressor_factory_ = [dictionary] {
        return internal::ZstdDecompressor(dictionary);
      };
    }
  }

  [[nodiscard]] size_t size() const { return shard_indexing_.size(); }

  absl::Status ReadWithAllocator(
      size_t index,
      absl::FunctionRef<absl::Span<char>(size_t record_size)> allocate) const {
    if (index >= size()) {
      return absl::OutOfRangeError(
          absl::StrCat("Index ", index, " out of range [0, ", size(), ")"));
    }
    auto decompressor = decompressor_factory_ != nullptr
                            ? decompressor_pool_.Get(decompressor_factory_)
                            : nullptr;
    internal::ShardIndex shard_index = shard_indexing_.index(index);
    absl::Status compress_status = absl::OkStatus();

    absl::Status status = shards_[shard_index.shard].Read(
        shard_index.shard_index, [&](absl::string_view compressed) {
          compress_status =
              DecompressInto(decompressor.get(), compressed, allocate);
        });
    status.Update(compress_status);
    return status;
  }

  absl::StatusOr<BagzReader::Handle> ReadHandle(size_t index) const {
    internal::ShardIndex shard_index = shard_indexing_.index(index);
    absl::StatusOr<internal::BagzShardReader::ByteRange> byte_range =
        shards_[shard_index.shard].ReadByteRange(shard_index.shard_index);
    if (!byte_range.ok()) {
      return byte_range.status();
    }
    return BagzReader::Handle{.shard = shard_index.shard,
                              .offset = byte_range->offset,
                              .num_bytes = byte_range->length};
  }

  absl::Status ReadFromHandleWithAllocator(
      BagzReader::Handle handle,
      absl::FunctionRef<absl::Span<char>(size_t record_size)> allocate) const {
    auto decompressor = decompressor_factory_ != nullptr
                            ? decompressor_pool_.Get(decompressor_factory_)
                            : nullptr;
    absl::Status compress_status = absl::OkStatus();

    absl::Status status = shards_[handle.shard].ReadFromByteRange(
        internal::BagzShardReader::ByteRange{.offset = handle.offset,
                                             .length = handle.num_bytes},
        [&](absl::string_view compressed) {
          compress_status =
              DecompressInto(decompressor.get(), compressed, allocate);
        });
    status.Update(compress_status);
    return status;
  }

  absl::Status ReadRangeWithAllocator(
      size_t start, size_t num_records,
      absl::FunctionRef<absl::Span<char>(size_t result_index,
                                         size_t record_size)>
          allocate_for_index) const {
    if (!shard_indexing_.valid_range(start, num_records)) {
      return absl::OutOfRangeError(
          absl::StrCat("Range [", start, ", ", start + num_records,
                       ") out of range [0, ", size(), "]"));
    }

    return ReadShardRanges(shard_indexing_.range(start, num_records),
                           allocate_for_index);
  }

  absl::Status ReadIndicesWithAllocator(
      absl::Span<const size_t> indices,
      absl::FunctionRef<absl::Span<char>(size_t result_index,
                                         size_t record_size)>
          allocate_for_index,
      absl::FunctionRef<void(size_t from_index, size_t to_index)> copy_result)
      const {
    struct Result {
      internal::ShardIndex record;
      size_t result_index;
    };
    std::vector<Result> result_map;
    for (size_t i = 0; i < indices.size(); ++i) {
      size_t index = indices[i];
      if (index >= size()) {
        return absl::OutOfRangeError(absl::StrCat(
            "indices[", i, "] = ", index, " out of range [0, ", size(), ")"));
      }

      result_map.push_back(
          Result{.record = shard_indexing_.index(index), .result_index = i});
    }

    // Sort by record, result_index.
    absl::c_sort(result_map, [](const Result& a, const Result& b) {
      return std::tie(a.record.shard, a.record.shard_index, a.result_index) <
             std::tie(b.record.shard, b.record.shard_index, b.result_index);
    });

    std::vector<absl::Span<const Result>> grouped_by_record_index;
    grouped_by_record_index.reserve(result_map.size());
    // Remove duplicates.
    absl::Span<const Result> result_map_span = absl::MakeConstSpan(result_map);
    for (size_t i = 0; i < result_map.size();) {
      size_t record_shard = result_map[i].record.shard;
      size_t record_index = result_map[i].record.shard_index;
      size_t start_index = i++;
      while (i < result_map.size() &&
             result_map[i].record.shard_index == record_index &&
             result_map[i].record.shard == record_shard) {
        ++i;
      }
      grouped_by_record_index.push_back(
          result_map_span.subspan(start_index, i - start_index));
    }

    std::vector<internal::ShardRange> shard_ranges;

    // Upper bound on the number of shard ranges.

    // Group by contiguous ranges.
    shard_ranges.reserve(grouped_by_record_index.size());
    for (size_t i = 0; i < grouped_by_record_index.size();) {
      const Result& first = grouped_by_record_index[i].front();
      size_t record_shard = first.record.shard;
      size_t record_index = first.record.shard_index;
      size_t start_index = i++;
      for (; i < grouped_by_record_index.size(); ++i) {
        const Result& next = grouped_by_record_index[i].front();
        if (next.record.shard_index != record_index + i - start_index ||
            next.record.shard != record_shard) {
          break;
        }
      }
      shard_ranges.push_back(internal::ShardRange{.shard = record_shard,
                                                  .shard_start = record_index,
                                                  .count = i - start_index,
                                                  .result_offset = start_index,
                                                  .result_stride = 1});
    }
    if (absl::Status status = ReadShardRanges(
            shard_ranges,
            [&](size_t result_index, size_t record_size) {
              return allocate_for_index(
                  grouped_by_record_index[result_index].front().result_index,
                  record_size);
            });
        !status.ok()) {
      return status;
    }
    // Copy duplicates.
    if (grouped_by_record_index.size() < indices.size()) {
      for (absl::Span<const Result> group : grouped_by_record_index) {
        if (group.size() > 1) {
          size_t from_index = group.front().result_index;
          for (size_t i = 1; i < group.size(); ++i) {
            copy_result(from_index, group[i].result_index);
          }
        }
      }
    }
    return absl::OkStatus();
  }

  double ApproximateNumBytesPerRecord() const {
    size_t num_records = size();
    if (num_records == 0) {
      return 0.0;
    }
    size_t num_bytes = 0;
    for (const internal::BagzShardReader& shard : shards_) {
      num_bytes += shard.num_bytes();
    }
    return static_cast<double>(num_bytes) / num_records;
  }

  const BagzReader::Options& options() const { return options_; }

 private:
  static absl::Status DecompressInto(
      internal::ZstdDecompressor* decompressor, absl::string_view compressed,
      absl::FunctionRef<absl::Span<char>(size_t num_bytes)> allocate) {
    if (decompressor == nullptr || compressed.empty()) {
      auto span = allocate(compressed.size());
      if (span.size() != compressed.size()) {
        return absl::InternalError(
            absl::StrCat("Failed to allocate ", compressed.size(), " bytes"));
      }
      std::memcpy(span.data(), compressed.data(), compressed.size());
      return absl::OkStatus();
    } else {
      return decompressor->Decompress(compressed, allocate);
    }
  }

  absl::Status ReadShardRanges(
      absl::Span<const internal::ShardRange> shard_ranges,
      absl::FunctionRef<absl::Span<char>(size_t result_index,
                                         size_t record_size)>
          allocate_for_index) const {
    return internal::ParallelDo(
        shard_ranges.size(),
        [&](size_t shard_index) -> absl::Status {
          const internal::ShardRange& shard_range = shard_ranges[shard_index];
          absl::Status compress_status = absl::OkStatus();
          auto status = shards_[shard_range.shard].ReadRange(
              shard_range.shard_start, shard_range.count,
              [&](size_t index, absl::string_view compressed) {
                size_t result_index = shard_range.result_stride * index +
                                      shard_range.result_offset;
                auto decompressor =
                    decompressor_factory_ != nullptr
                        ? decompressor_pool_.Get(decompressor_factory_)
                        : nullptr;
                compress_status = DecompressInto(
                    decompressor.get(), compressed,
                    [result_index, &allocate_for_index](size_t num_bytes) {
                      return allocate_for_index(result_index, num_bytes);
                    });
                return compress_status.ok();
              });
          status.Update(compress_status);
          return status;
        },
        options_.max_parallelism,
        /*cpu_bound=*/decompressor_factory_ != nullptr);
  }

  Options options_;
  std::vector<internal::BagzShardReader> shards_;
  internal::ShardIndexing shard_indexing_;
  mutable internal::RecyclingPool<internal::ZstdDecompressor>
      decompressor_pool_;
  absl::AnyInvocable<internal::ZstdDecompressor() const> decompressor_factory_;
};

size_t BagzReader::size() const { return slice_length_; }

double BagzReader::ApproximateNumBytesPerRecord() const {
  return state_->ApproximateNumBytesPerRecord();
}

absl::StatusOr<std::string> BagzReader::operator[](size_t index) const {
  if (index >= slice_length_) {
    return absl::OutOfRangeError(absl::StrCat(
        "index ", index, " out of range [0, ", slice_length_, ")"));
  }
  absl::StatusOr<BagzReader::Handle> handle =
      state_->ReadHandle(index * slice_step_ + slice_start_);
  if (!handle.ok()) {
    return handle.status();
  }
  return ReadFromHandle(*handle);
}

absl::StatusOr<std::vector<std::string>> BagzReader::ReadRange(
    size_t start, size_t num_records) const {
  if (start + num_records > slice_length_) {
    return absl::OutOfRangeError(
        absl::StrCat("Range [", start, ", ", start + num_records,
                     ") out of range [0, ", slice_length_, "]"));
  }
  std::vector<std::string> result_vector(num_records);
  if (slice_step_ != 1) {
    std::vector<size_t> indices;
    indices.reserve(num_records);
    for (size_t i = 0; i < num_records; ++i) {
      indices.push_back(slice_start_ + (start + i) * slice_step_);
    }
    if (absl::Status status = state_->ReadIndicesWithAllocator(
            indices,
            [&result_vector](size_t result_index, size_t record_size) {
              result_vector[result_index].resize(record_size);
              return absl::MakeSpan(result_vector[result_index]);
            },
            [&result_vector](size_t from_index, size_t to_index) {
              result_vector[to_index] = result_vector[from_index];
            });
        !status.ok()) {
      return status;
    } else {
      return result_vector;
    }
  }
  if (absl::Status status = state_->ReadRangeWithAllocator(
          start + slice_start_, num_records,
          [&result_vector](size_t result_index, size_t record_size) {
            result_vector[result_index].resize(record_size);
            return absl::MakeSpan(result_vector[result_index]);
          });
      !status.ok()) {
    return status;
  } else {
    return result_vector;
  }
}

absl::StatusOr<std::vector<std::string>> BagzReader::ReadIndices(
    absl::Span<const size_t> indices) const {
  std::vector<std::size_t> indices_vector;
  if (slice_step_ != 1 || slice_start_ != 0) {
    indices_vector.reserve(indices.size());
    size_t position = 0;
    for (size_t index : indices) {
      if (index >= slice_length_) {
        return absl::OutOfRangeError(
            absl::StrCat("indices[", position, "] = ", index,
                         " out of range [0, ", slice_length_, ")"));
      }
      ++position;
      indices_vector.push_back(index * slice_step_ + slice_start_);
    }
    indices = absl::MakeSpan(indices_vector);
  }

  std::vector<std::string> result_vector(indices.size());
  if (absl::Status status = state_->ReadIndicesWithAllocator(
          indices,
          [&result_vector](size_t result_index, size_t record_size) {
            result_vector[result_index].resize(record_size);
            return absl::MakeSpan(result_vector[result_index]);
          },
          [&result_vector](size_t from_index, size_t to_index) {
            result_vector[to_index] = result_vector[from_index];
          });
      !status.ok()) {
    return status;
  } else {
    return result_vector;
  }
}

absl::Status BagzReader::ReadWithAllocator(
    size_t index,
    absl::FunctionRef<absl::Span<char>(size_t record_size)> allocate) const {
  if (index >= slice_length_) {
    return absl::OutOfRangeError(absl::StrCat(
        "index ", index, " out of range [0, ", slice_length_, ")"));
  }
  return state_->ReadWithAllocator(index * slice_step_ + slice_start_,
                                   allocate);
}

absl::Status BagzReader::ReadRangeWithAllocator(
    size_t start, size_t num_records,
    absl::FunctionRef<absl::Span<char>(size_t result_index, size_t record_size)>
        allocate_for_index) const {
  if (start + num_records > slice_length_) {
    return absl::OutOfRangeError(
        absl::StrCat("Range ", start, ", ", start + num_records,
                     " out of range [0, ", slice_length_, "]"));
  }
  if (slice_step_ != 1) {
    if (slice_step_ == 0) {
      return absl::InvalidArgumentError(
          "slice_step_ must not be 0 when calling ReadRangeWithAllocator().");
    }
    std::vector<size_t> indices;
    indices.reserve(num_records);
    for (size_t i = 0; i < num_records; ++i) {
      indices.push_back(slice_start_ + (start + i) * slice_step_);
    }
    return state_->ReadIndicesWithAllocator(indices, allocate_for_index,
                                            [](size_t, size_t) {});
  }

  return state_->ReadRangeWithAllocator(start + slice_start_, num_records,
                                        allocate_for_index);
}

absl::Status BagzReader::ReadIndicesWithAllocator(
    absl::Span<const size_t> indices,
    absl::FunctionRef<absl::Span<char>(size_t result_index, size_t record_size)>
        allocate_for_index,
    absl::FunctionRef<void(size_t from_index, size_t to_index)> copy_result)
    const {
  std::vector<size_t> indices_vector;
  if (slice_step_ != 1 || slice_start_ != 0) {
    indices_vector.reserve(indices.size());
    size_t position = 0;
    for (size_t index : indices) {
      if (index >= slice_length_) {
        return absl::OutOfRangeError(
            absl::StrCat("indices[", position, "] = ", index,
                         " out of range [0, ", slice_length_, ")"));
      }
      ++position;
      indices_vector.push_back(index * slice_step_ + slice_start_);
    }
    indices = absl::MakeSpan(indices_vector);
  }
  return state_->ReadIndicesWithAllocator(indices, allocate_for_index,
                                          copy_result);
}

absl::StatusOr<BagzReader> BagzReader::OpenFiles(
    absl::Span<absl_nonnull std::unique_ptr<PReadFile>> record_files,
    absl::Span<absl_nonnull std::unique_ptr<PReadFile>> limits_files,
    Options options) {
  if (std::holds_alternative<CompressionAutoDetect>(options.compression)) {
    return absl::InvalidArgumentError(
        "Compression must not be kAutoDetect when calling "
        "BagzReader::OpenFiles().");
  }

  std::vector<std::unique_ptr<PReadFile>> limit_files_vector;
  if (options.limits_placement == LimitsPlacement::kSeparate) {
    if (record_files.size() != limits_files.size()) {
      return absl::InvalidArgumentError(
          "When LimitsPlacement::kSeparate, Then record_files.size() must "
          "equal limits_files.size().");
    }
  } else {
    if (!limits_files.empty()) {
      return absl::InvalidArgumentError(
          "When LimitsPlacement::kSeparate, Then limits_files.empty() must "
          "be true.");
    }
    limit_files_vector.reserve(record_files.size());
    for (std::size_t i = 0; i < record_files.size(); ++i) {
      limit_files_vector.push_back(nullptr);
    }
    limits_files = absl::MakeSpan(limit_files_vector);
    absl::Status status = internal::ParallelDo(
        record_files.size(),
        [&](size_t i) -> absl::Status {
          absl::StatusOr<internal::RecordsLimits> split =
              internal::SplitRecordsAndLimits(std::move(record_files[i]));
          if (!split.ok()) {
            return absl::Status(
                split.status().code(),
                absl::StrCat(split.status().message(), "; at file ", i));
          }
          record_files[i] = std::move(split->records);
          limit_files_vector[i] = std::move(split->limits);
          return absl::OkStatus();
        },
        options.max_parallelism, /*cpu_bound=*/false);

    if (!status.ok()) {
      return status;
    }
  }
  std::vector<internal::BagzShardReader> shards;
  size_t total_count = 0;
  std::vector<size_t> accumulated_count;
  for (size_t i = 0; i < record_files.size(); ++i) {
    if (options.limits_storage == LimitsStorage::kInMemory) {
      limits_files[i] =
          std::make_unique<InMemoryPReadFile>(std::move(limits_files[i]));
    }

    internal::BagzShardReader& shard = shards.emplace_back(
        std::move(record_files[i]), std::move(limits_files[i]));
    total_count += shard.size();
    accumulated_count.push_back(total_count);
  }
  if (options.sharding_layout == ShardingLayout::kInterleaved) {
    for (size_t i = 1; i < shards.size(); ++i) {
      if (shards[i].size() > shards[i - 1].size()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Interleaved error - shard sizes must be non-increasing. Size at ",
            i, " : ", shards[i].size(), " size at", i - 1, " : ",
            shards[i - 1].size()));
      }
      if (shards.front().size() - shards[i].size() > 1) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Interleaved error - shard sizes must be within 1 of each other. "
            "First size: ",
            shards.front().size(), " size at ", i, " : ", shards[i].size()));
      }
    }
  }
  return BagzReader(
      std::make_shared<State>(std::move(options), std::move(shards),
                              std::move(accumulated_count)),
      /*slice_start=*/0, /*slice_step=*/1, /*slice_length=*/total_count);
}

absl::StatusOr<BagzReader> BagzReader::Open(absl::string_view filespec,
                                            BagzReader::Options options) {
  if (std::holds_alternative<CompressionAutoDetect>(options.compression)) {
    if (filespec.ends_with(".bagz")) {
      options.compression.emplace<CompressionZstd>();
    } else {
      options.compression.emplace<CompressionNone>();
    }
  }

  absl::StatusOr<std::vector<std::unique_ptr<PReadFile>>> all_files;
  absl::Span<std::unique_ptr<PReadFile>> record_files;
  absl::Span<std::unique_ptr<PReadFile>> limits_files;
  if (options.limits_placement == LimitsPlacement::kSeparate) {
    all_files = file::BulkOpenPRead(
        absl::StrCat(filespec, ",", internal::LimitsName(filespec)));
    if (!all_files.ok()) {
      return all_files.status();
    }
    size_t num_files = all_files->size() / 2;
    record_files = absl::MakeSpan(*all_files).subspan(0, num_files);
    limits_files = absl::MakeSpan(*all_files).subspan(num_files);
  } else {
    all_files = file::BulkOpenPRead(filespec);
    if (!all_files.ok()) {
      return all_files.status();
    }
    record_files = absl::MakeSpan(*all_files);
  }
  return OpenFiles(record_files, limits_files, std::move(options));
}

absl::StatusOr<BagzReader::Handle> BagzReader::ReadHandle(size_t index) const {
  if (index >= slice_length_) {
    return absl::OutOfRangeError(absl::StrCat(
        "index ", index, " out of range [0, ", slice_length_, ")"));
  }
  return state_->ReadHandle(index * slice_step_ + slice_start_);
}

absl::StatusOr<std::string> BagzReader::ReadFromHandle(Handle handle) const {
  std::string result;
  if (absl::Status status =
          state_->ReadFromHandleWithAllocator(handle,
                                              [&result](size_t record_size) {
                                                result.resize(record_size);
                                                return absl::MakeSpan(result);
                                              });
      !status.ok()) {
    return status;
  } else {
    return result;
  }
}

absl::Status BagzReader::ReadFromHandleWithAllocator(
    Handle handle,
    absl::FunctionRef<absl::Span<char>(size_t record_size)> allocate) const {
  return state_->ReadFromHandleWithAllocator(handle, allocate);
}

absl::StatusOr<BagzReader> BagzReader::Slice(size_t start, int64_t step,
                                             size_t length) const {
  size_t num_records = size();
  if (step == 0) {
    return absl::OutOfRangeError("step must be non-zero");
  }
  if (length > 0) {
    if (start >= num_records) {
      return absl::OutOfRangeError(absl::StrCat(
          "start (", start, ") must be in range [0, ", num_records, ")"));
    }
    if (step > 0) {
      if (start + step * (length - 1) + 1 > num_records) {
        return absl::OutOfRangeError(absl::StrCat(
            "(start: ", start, " + step: ", step, " * length: ", length,
            ") must be less than size: ", num_records));
      }
    } else {
      // step < 0.
      if (start < -step * (length - 1)) {
        return absl::OutOfRangeError(absl::StrCat(
            "(start: ", start, ", step: ", step, ", length: ", length,
            ") doesn't fit in [0, ", num_records, ")"));
      }
    }
  }
  start = slice_start_ + start * slice_step_;
  step *= slice_step_;
  return BagzReader(state_, length != 0 ? start : 0, step, length);
}

const BagzReader::Options& BagzReader::options() const {
  return state_->options();
}

}  // namespace bagz
