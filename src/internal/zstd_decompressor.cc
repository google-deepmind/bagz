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

#include "src/internal/zstd_decompressor.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "zstd.h"

namespace bagz::internal {

ZstdDecompressor::ZstdDecompressor(absl::string_view dictionary)
    : ctx_(ZSTD_createDCtx()),
      cdict_(ZSTD_createDDict(dictionary.data(), dictionary.size())) {
  ZSTD_DCtx_refDDict(ctx_.get(), cdict_.get());
}

absl::Status ZstdDecompressor::Decompress(
    absl::string_view compressed,
    absl::FunctionRef<absl::Span<char>(size_t size)> allocate_output) {
  if (cdict_ == nullptr) {
    // Invalid dictionary was provided.
    return absl::InvalidArgumentError("Invalid dictionary");
  }
  size_t output_size =
      ZSTD_getFrameContentSize(compressed.data(), compressed.size());
  if (output_size == ZSTD_CONTENTSIZE_ERROR) {
    return absl::InvalidArgumentError("Invalid compressed data");
  }
  if (output_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return DecompressStreamed(compressed, allocate_output);
  }
  auto result = allocate_output(output_size);
  if (result.size() != output_size) {
    return absl::InternalError(
        absl::StrCat("Failed to allocate ", output_size, " bytes"));
  }
  size_t decompressed_size =
      ZSTD_decompressDCtx(ctx_.get(), result.data(), result.size(),
                          compressed.data(), compressed.size());
  if (ZSTD_isError(decompressed_size)) {
    ZSTD_DCtx_reset(ctx_.get(), ZSTD_reset_session_only);
    return absl::InvalidArgumentError("Invalid compressed data");
  }
  return absl::OkStatus();
}

absl::Status ZstdDecompressor::DecompressStreamed(
    absl::string_view compressed,
    absl::FunctionRef<absl::Span<char>(size_t size)> allocate_output) {
  if (dstream_ == nullptr) {
    dstream_.reset(ZSTD_createDStream());
  }
  const size_t output_buffer_size = ZSTD_DStreamOutSize();
  struct OutputBuffer {
    std::unique_ptr<char[]> buffer;
    size_t size;
  };

  std::vector<OutputBuffer> output_buffers;
  ZSTD_inBuffer input = {compressed.data(), compressed.size(), 0};
  size_t output_size = 0;
  while (input.pos < input.size) {
    auto output_buffer =
        std::make_unique_for_overwrite<char[]>(output_buffer_size);
    ZSTD_outBuffer output = {output_buffer.get(), output_buffer_size, 0};
    size_t const ret = ZSTD_decompressStream(ctx_.get(), &output, &input);
    if (ZSTD_isError(ret)) {
      ZSTD_DCtx_reset(ctx_.get(), ZSTD_reset_session_only);
      return absl::InvalidArgumentError("Invalid compressed data");
    }
    output_size += output.pos;
    output_buffers.push_back(
        OutputBuffer{std::move(output_buffer), output.pos});
  }
  absl::Span<char> result = allocate_output(output_size);
  if (result.size() != output_size) {
    return absl::InternalError("Output size does not match expected size");
  }
  for (const auto& output_buffer : output_buffers) {
    std::memcpy(result.data(), output_buffer.buffer.get(), output_buffer.size);
    result = result.subspan(output_buffer.size);
  }
  return absl::OkStatus();
}

}  // namespace bagz::internal
