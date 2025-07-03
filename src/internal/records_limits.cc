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

#include "src/internal/records_limits.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

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

class OffsetPReadFileRef : public PReadFile {
 public:
  explicit OffsetPReadFileRef(std::shared_ptr<PReadFile> pread_file,
                              size_t offset, size_t size)
      : pread_file_(pread_file), offset_(offset), size_(size) {}

  ~OffsetPReadFileRef() override = default;

  size_t size() const override { return size_; };

  absl::Status PRead(
      size_t offset, size_t num_bytes,
      absl::FunctionRef<bool(absl::string_view)> callback) const override {
    if (num_bytes > size_) {
      return absl::OutOfRangeError(
          absl::StrCat("num_bytes(", num_bytes, " >  file size(", size_, ")"));
    }
    if (offset > size_ - num_bytes) {
      return absl::OutOfRangeError(absl::StrCat("offset(", offset,
                                                " >  file size - num_bytes(",
                                                size_ - num_bytes, ")"));
    }
    return pread_file_->PRead(offset_ + offset, num_bytes, callback);
  }

 private:
  std::shared_ptr<PReadFile> pread_file_;
  size_t offset_;
  size_t size_;
};

absl::Status ReadIntoUint64(PReadFile& file, size_t offset,
                            absl::Span<uint64_t> value) {
  char* write_buffer = reinterpret_cast<char*>(value.data());
  absl::Status status =
      file.PRead(offset, value.size() * sizeof(uint64_t),
                 [&write_buffer](absl::string_view chunk) {
                   std::memcpy(write_buffer, chunk.data(), chunk.size());
                   write_buffer += chunk.size();
                   return true;
                 });
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

absl::StatusOr<RecordsLimits> SplitRecordsAndLimits(
    std::unique_ptr<PReadFile> bag_content) {
  std::shared_ptr<PReadFile> bag_content_shared = std::move(bag_content);
  size_t bag_size = bag_content_shared->size();
  if (bag_size < sizeof(uint64_t)) {
    if (bag_size != 0) {
      return absl::InvalidArgumentError(
          "Bad file format - invalid split point (non-empty but too small.)");
    } else {
      return RecordsLimits{
          std::make_unique<OffsetPReadFileRef>(bag_content_shared, 0, 0),
          std::make_unique<OffsetPReadFileRef>(std::move(bag_content_shared), 0,
                                               0)};
    }
  }
  uint64_t records_end = 0;
  const uint64_t last_offset = bag_size - sizeof(uint64_t);

  // Read the last 8 bytes to find split point.
  if (absl::Status status = ReadIntoUint64(*bag_content_shared, last_offset,
                                           absl::MakeSpan(&records_end, 1));
      !status.ok()) {
    return status;
  }

  // Check that the split point is within the bounds of the bag_content.
  // bag_size >= sizeof(uint64_t) is checked above.
  if (records_end > bag_size - sizeof(uint64_t)) {
    return absl::InvalidArgumentError(
        "Bad file format - invalid split point (too large offset).");
  }

  // Check that the limits size is a multiple of 8.
  size_t limits_size = (bag_size - records_end);
  if (limits_size % sizeof(uint64_t) != 0) {
    return absl::InvalidArgumentError(
        "Bad file format - invalid split point (invalid alignment).");
  }

  return RecordsLimits{
      std::make_unique<OffsetPReadFileRef>(bag_content_shared, 0, records_end),
      std::make_unique<OffsetPReadFileRef>(std::move(bag_content_shared),
                                           records_end, limits_size)};
}

}  // namespace bagz::internal
