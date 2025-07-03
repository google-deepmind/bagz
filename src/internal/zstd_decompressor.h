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

#ifndef BAGZ_SRC_INTERNAL_ZSTD_DECOMPRESSOR_H_
#define BAGZ_SRC_INTERNAL_ZSTD_DECOMPRESSOR_H_

#include <cstddef>
#include <memory>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "zstd.h"

namespace bagz::internal {

// C++ wrapper over ZStandard (ZSTD) decompression functions.
class ZstdDecompressor {
 public:
  // Creates compressor with given compression level and optional dictionary.
  // Dictionary must be a valid ZSTD dictionary.
  explicit ZstdDecompressor(absl::string_view dictionary);

  // Decompresses the given `compressed` data. Result is written to the buffer
  // returned by `allocate_output`. `allocate_output` must return a buffer of
  // given size this is where the decompressed data is written to.
  // `allocate_output` will be called only once on success and none or once on
  // failure depending on the error.
  //
  // Thread-compatible - do not call from multiple threads with out appropriate
  // synchronization.
  absl::Status Decompress(
      absl::string_view compressed,
      absl::FunctionRef<absl::Span<char>(size_t size)> allocate_output);

 private:
  // Bagz does not write streamed data, but we want to support reading it.
  absl::Status DecompressStreamed(
      absl::string_view compressed,
      absl::FunctionRef<absl::Span<char>(size_t size)> allocate_output);

  struct DtxDeleter {
    void operator()(ZSTD_DCtx* ptr) const { ZSTD_freeDCtx(ptr); }
  };
  struct DDictDeleter {
    void operator()(ZSTD_DDict* ptr) const { ZSTD_freeDDict(ptr); }
  };
  struct DStreamDeleter {
    void operator()(ZSTD_DStream* ptr) const { ZSTD_freeDStream(ptr); }
  };

  std::unique_ptr<ZSTD_DCtx, DtxDeleter> ctx_;
  std::unique_ptr<ZSTD_DDict, DDictDeleter> cdict_;
  std::unique_ptr<ZSTD_DStream, DStreamDeleter> dstream_;

  std::unique_ptr<char[]> output_buffer_;
};

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_ZSTD_DECOMPRESSOR_H_
