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

#ifndef BAGZ_SRC_INTERNAL_ZSTD_COMPRESSOR_H_
#define BAGZ_SRC_INTERNAL_ZSTD_COMPRESSOR_H_

#include <cstddef>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "zstd.h"

namespace bagz::internal {

// C++ wrapper over ZStandard (ZSTD) compression functions.
class ZstdCompressor {
 public:
  // Creates compressor with given compression level and optional dictionary.
  explicit ZstdCompressor(int level, absl::string_view dictionary);

  // Compresses the given record. The compressed output is stored in an internal
  // buffer, and a view of that buffer is returned. This is not thread-safe, and
  // the buffer is invalidated on the next call and on destruction.
  absl::StatusOr<absl::string_view> Compress(absl::string_view record);

 private:
  std::unique_ptr<char[]> compression_buffer_;
  size_t buffer_size_ = 0;

  struct CtxDeleter {
    void operator()(ZSTD_CCtx* ptr) const { ZSTD_freeCCtx(ptr); }
  };

  struct CDictDeleter {
    void operator()(ZSTD_CDict* ptr) const { ZSTD_freeCDict(ptr); }
  };

  std::unique_ptr<ZSTD_CCtx, CtxDeleter> ctx_;
  std::unique_ptr<ZSTD_CDict, CDictDeleter> cdict_;
};

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_ZSTD_COMPRESSOR_H_
