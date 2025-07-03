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

#include "src/internal/zstd_compressor.h"

#include <algorithm>
#include <cstddef>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "zstd.h"

namespace bagz::internal {

constexpr size_t kCompressionBufferStartingSize = 1024ULL * 128ULL;  // 128 KiB.

ZstdCompressor::ZstdCompressor(int level, absl::string_view dictionary)
    : ctx_(ZSTD_createCCtx()),
      cdict_(ZSTD_createCDict(dictionary.data(), dictionary.size(), level)) {}

// Returns view of internal buffer.
absl::StatusOr<absl::string_view> ZstdCompressor::Compress(
    absl::string_view record) {
  if (cdict_ == nullptr) {
    // Invalid dictionary was provided.
    return absl::InvalidArgumentError("Invalid dictionary");
  }
  const size_t max_compressed_size = ZSTD_compressBound(record.size());
  if (max_compressed_size > buffer_size_) {
    buffer_size_ =
        std::max(max_compressed_size, kCompressionBufferStartingSize);
    // Delete the old buffer before allocating the new one.
    compression_buffer_ = nullptr;
    compression_buffer_ = std::make_unique_for_overwrite<char[]>(buffer_size_);
  }
  size_t compressed_size = ZSTD_compress_usingCDict(
      ctx_.get(), compression_buffer_.get(), max_compressed_size, record.data(),
      record.size(), cdict_.get());

  if (ZSTD_isError(compressed_size)) {
    return absl::InternalError("Compression failed");
  }
  return absl::string_view(compression_buffer_.get(), compressed_size);
}

}  // namespace bagz::internal
