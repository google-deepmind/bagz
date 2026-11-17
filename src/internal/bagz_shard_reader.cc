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

#include "src/internal/bagz_shard_reader.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/file/file_system/pread_file.h"
#include "src/internal/serialize.h"

namespace bagz::internal {
namespace {

absl::Status ReadIntoUint64(PReadFile& file, size_t offset,
                            absl::Span<uint64_t> value) {
  absl::Status status =
      file.PRead(offset, absl::MakeSpan(reinterpret_cast<char*>(value.data()),
                                        value.size() * sizeof(uint64_t)));
  if constexpr (std::endian::native == std::endian::big) {
    if (status.ok()) {
      for (uint64_t& value : value) {
        value = SerializeUint64(value);
      }
    }
  }
  return status;
}

}  // namespace

absl::StatusOr<BagzShardReader::ByteRange> BagzShardReader::ReadByteRange(
    size_t index) const {
  if (index >= size()) {
    return absl::OutOfRangeError(
        absl::StrCat("index(", index, ") >= size(", size(), ")"));
  }
  const size_t records_size = records_->size();
  if (records_size == 0) {
    return ByteRange{0, 0};
  }
  uint64_t range[2] = {0, 0};
  if (index == 0) {
    if (absl::Status status =
            ReadIntoUint64(*limits_, 0, absl::MakeSpan(range + 1, 1));
        !status.ok()) {
      return status;
    }
  } else if (absl::Status status =
                 ReadIntoUint64(*limits_, (index - 1) * sizeof(uint64_t),
                                absl::MakeSpan(range, 2));
             !status.ok()) {
    return status;
  }

  if (range[0] <= range[1] && range[1] <= records_size) {
    return ByteRange{range[0], range[1] - range[0]};
  } else {
    return absl::InvalidArgumentError("Bad file format.");
  }
}

absl::Status BagzShardReader::ReadFromByteRange(
    const ByteRange& byte_range,
    absl::FunctionRef<void(absl::string_view)> callback) const {
  if (byte_range.offset <= records_->size() && byte_range.length == 0) {
    // Zero bytes record.
    callback(absl::string_view{});
    return absl::OkStatus();
  }
  auto buffer = std::make_unique_for_overwrite<char[]>(byte_range.length);
  if (absl::Status status = records_->PRead(
          byte_range.offset, absl::MakeSpan(buffer.get(), byte_range.length));
      !status.ok()) {
    return status;
  }
  callback(absl::string_view(buffer.get(), byte_range.length));
  return absl::OkStatus();
}

absl::Status BagzShardReader::Read(
    size_t index,
    absl::FunctionRef<void(absl::string_view record)> callback) const {
  if (auto byte_range = ReadByteRange(index); byte_range.ok()) {
    return ReadFromByteRange(*byte_range, callback);
  } else {
    return std::move(byte_range).status();
  }
}

absl::Status BagzShardReader::ReadLimits(size_t index, size_t count,
                                         absl::Span<uint64_t> limits) const {
  const size_t num_records = size();
  // Prevent overflow.
  if (count > num_records || index > (num_records - count)) {
    return absl::OutOfRangeError("");
  }
  if (limits.size() != count + 1) {
    return absl::InvalidArgumentError("Bad limits span.");
  }
  if (count == 0) {
    limits[0] = 0;
    return absl::OkStatus();
  }
  // All empty records.
  if (records_->size() == 0) {
    absl::c_fill(limits, 0);
    return absl::OkStatus();
  }
  if (index == 0) {
    limits[0] = 0;
    return ReadIntoUint64(*limits_, 0,
                          absl::Span<uint64_t>(limits.data() + 1, count));
  } else {
    return ReadIntoUint64(*limits_, (index - 1) * sizeof(uint64_t),
                          absl::Span<uint64_t>(limits.data(), count + 1));
  }
}

absl::Status BagzShardReader::ReadFromLimits(
    absl::Span<const uint64_t> limits,
    absl::FunctionRef<bool(size_t index, absl::string_view)> callback) const {
  const size_t records_size = records_->size();
  if (limits.size() < 2) {
    if (limits.empty()) {
      return absl::InvalidArgumentError("Bad limits span. (Empty limits.)");
    } else if (limits.front() > records_size) {
      return absl::InvalidArgumentError("Bad file format. (Invalid limits.)");
    }
    return absl::OkStatus();
  }
  if (absl::c_adjacent_find(limits, [](uint64_t previous, uint64_t next) {
        return previous > next;
      }) != limits.end()) {
    return absl::InvalidArgumentError(
        "Bad file format. (Limits are not monotonically increasing.)");
  }
  // Monotonically increasing limits so only need to check the last one.
  if (limits.back() > records_size) {
    return absl::InvalidArgumentError(
        "Bad file format. (Contains limits that are larger than records "
        "size.)");
  }

  const size_t num_bytes = limits.back() - limits.front();
  if (num_bytes == 0) {
    for (size_t i = 0; i + 1 < limits.size(); ++i) {
      if (!callback(i, absl::string_view{})) {
        return absl::OkStatus();
      }
    }
    return absl::OkStatus();
  }

  auto buffer = std::make_unique_for_overwrite<char[]>(num_bytes);
  if (absl::Status status = records_->PRead(
          limits.front(), absl::MakeSpan(buffer.get(), num_bytes));
      !status.ok()) {
    return status;
  }

  const uint64_t base_offset = limits.front();
  for (size_t i = 0; i + 1 < limits.size(); ++i) {
    const uint64_t start = limits[i] - base_offset;
    const uint64_t end = limits[i + 1] - base_offset;
    if (!callback(i, absl::string_view(buffer.get() + start, end - start))) {
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status BagzShardReader::ReadRange(
    size_t index, size_t count,
    absl::FunctionRef<bool(size_t index, absl::string_view)> callback) const {
  const size_t num_records = size();
  // Prevent overflow.
  if (count > num_records) {
    return absl::OutOfRangeError(
        absl::StrCat("count(", count, ") > num_records(", num_records, ")"));
  }
  if (index > (num_records - count)) {
    return absl::OutOfRangeError(absl::StrCat("index(", index,
                                              ") > max_index for count (",
                                              num_records - count, ")"));
  }
  if (count == 0) {
    return absl::OkStatus();
  }
  if (records_->size() == 0) {
    for (size_t i = 0; i < count; ++i) {
      if (!callback(i, absl::string_view{})) {
        return absl::OkStatus();
      }
    }
    return absl::OkStatus();
  }
  auto split_points_memory = std::make_unique<uint64_t[]>(count + 1);
  auto split_points = absl::MakeSpan(split_points_memory.get(), count + 1);
  if (absl::Status status = ReadLimits(index, count, split_points);
      !status.ok()) {
    return status;
  }
  return ReadFromLimits(split_points, callback);
};

}  // namespace bagz::internal
