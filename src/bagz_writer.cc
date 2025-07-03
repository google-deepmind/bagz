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

#include "src/bagz_writer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/bagz_options.h"
#include "src/file/file.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/write_file.h"
#include "src/internal/limits_name.h"
#include "src/internal/serialize.h"
#include "src/internal/zstd_compressor.h"
#include "zstd.h"

namespace bagz {

namespace {

constexpr size_t kAppendBufferSize = 1024ull * 128ull;  // 128 KiB.

// Appends the contents of `src` to the end of `dst`.
absl::Status AppendSourceToDest(PReadFile& src, WriteFile& dst) {
  absl::Status write_status = absl::OkStatus();
  for (std::size_t offset = 0; offset < src.size();
       offset += kAppendBufferSize) {
    size_t length = std::min(kAppendBufferSize, src.size() - offset);
    absl::Status read_status =
        src.PRead(offset, length, [&](absl::string_view buffer) {
          write_status = dst.Write(buffer);
          return write_status.ok();
        });
    read_status.Update(write_status);
    if (!read_status.ok()) {
      return read_status;
    }
  }
  return absl::OkStatus();
}

}  // namespace

class BagzWriter::State {
 public:
  // records_file file must be opened with "wb";
  // limits_file file must be opened with "w+b" if LimitsPlacement::kTail;
  State(BagzWriter::Options options, std::unique_ptr<WriteFile> records_file,
        std::unique_ptr<WriteFile> limits_file, std::string limits_filename)
      : records_file_(std::move(records_file)),
        limits_file_(std::move(limits_file)),
        limits_filename_(std::move(limits_filename)),
        options_(std::move(options)) {
    if (std::holds_alternative<CompressionZstd>(options_.compression)) {
      const auto& zstd = std::get<CompressionZstd>(options_.compression);
      compressor_.emplace(zstd.level, zstd.dictionary);
    }
  }

  absl::Status Write(absl::string_view record) {
    if (compressor_.has_value() && !record.empty()) {
      if (absl::StatusOr<absl::string_view> compressed_record =
              compressor_->Compress(record);
          !compressed_record.ok()) {
        return compressed_record.status();
      } else {
        record = *compressed_record;
      }
    }
    if (absl::Status status = records_file_->Write(record); !status.ok()) {
      return status;
    }
    records_size_ += record.size();
    uint64_t serialized_pos = internal::SerializeUint64(records_size_);
    return limits_file_->Write(
        absl::string_view(reinterpret_cast<const char*>(&serialized_pos),
                          sizeof(serialized_pos)));
  }

  absl::Status Flush() {
    absl::Status status;
    status.Update(limits_file_->Flush());
    status.Update(records_file_->Flush());
    return status;
  }

  absl::Status Close() {
    // Free the compressor.
    compressor_ = std::nullopt;
    if (absl::Status status = limits_file_->Close(); !status.ok()) {
      return status;
    }
    if (options_.limits_placement == LimitsPlacement::kTail) {
      auto limits_file_read = file::OpenPRead(limits_filename_);
      if (!limits_file_read.ok()) {
        return limits_file_read.status();
      }
      if (absl::Status status =
              AppendSourceToDest(**limits_file_read, *records_file_);
          !status.ok()) {
        return status;
      }
    }

    if (absl::Status status = records_file_->Close(); !status.ok()) {
      return status;
    }

    if (options_.limits_placement == LimitsPlacement::kTail) {
      return file::Delete(limits_filename_);
    } else {
      return absl::OkStatus();
    }
  }

 private:
  std::unique_ptr<WriteFile> records_file_;
  std::unique_ptr<WriteFile> limits_file_;
  std::optional<internal::ZstdCompressor> compressor_;
  std::string limits_filename_;
  BagzWriter::Options options_;
  uint64_t records_size_ = 0;
};

absl::StatusOr<BagzWriter> BagzWriter::OpenFile(absl::string_view filename,
                                                BagzWriter::Options options) {
  if (std::holds_alternative<CompressionAutoDetect>(options.compression)) {
    if (filename.ends_with(".bagz")) {
      options.compression.emplace<CompressionZstd>();
    } else {
      options.compression.emplace<CompressionNone>();
    }
  }
  auto records_file = file::OpenWrite(filename);
  if (!records_file.ok()) {
    return records_file.status();
  }
  std::string limits_filename = internal::LimitsName(filename);
  auto limits_file = file::OpenWrite(limits_filename);
  if (!limits_file.ok()) {
    return limits_file.status();
  }

  return BagzWriter(std::make_unique<State>(
      std::move(options), *std::move(records_file), *std::move(limits_file),
      std::move(limits_filename)));
}

absl::Status BagzWriter::Write(absl::string_view record) {
  if (state_ == nullptr) {
    return absl::FailedPreconditionError("Closed");
  }
  return state_->Write(record);
}

absl::Status BagzWriter::Flush() {
  if (state_ == nullptr) {
    return absl::FailedPreconditionError("Closed");
  }
  return state_->Flush();
}

absl::Status BagzWriter::Close() {
  if (state_ == nullptr) {
    return absl::OkStatus();
  }
  auto result = state_->Close();
  state_.reset();
  return result;
}

BagzWriter::~BagzWriter() = default;

BagzWriter::BagzWriter(BagzWriter&&) = default;

BagzWriter::BagzWriter(std::unique_ptr<State> state)
    : state_(std::move(state)) {}

}  // namespace bagz
