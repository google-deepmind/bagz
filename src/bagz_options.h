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

#ifndef BAGZ_SRC_BAGZ_OPTIONS_H_
#define BAGZ_SRC_BAGZ_OPTIONS_H_

#include <cstddef>
#include <string>
#include <variant>

namespace bagz {

// Whether limits are at the end of the file or in a separate file. See
// README.md#limits.
enum class LimitsPlacement {
  kTail,      // Limits section is at the end of the file.
  kSeparate,  // Limits section is in a separate file with `limits.` prefix.
};

// Specifies CompressionZstd{} if the filename ends with `.bagz`, otherwise
// specifies CompressionNone{}.
struct CompressionAutoDetect {};

// Specifies no compression.
struct CompressionNone {};

// Specifies ZSTD compression.
struct CompressionZstd {
  std::string dictionary;  // Optional dictionary for decompression.
  int level = 0;           // Compression level, ignored by `Reader`.
};

// Specifies what type of compression is used.
using Compression =
    std::variant<CompressionAutoDetect, CompressionNone, CompressionZstd>;

// Reader only options.

// When opening more than one shard for a bag, the ShardingLayout type specifies
// how the records in the bag are indexed. See README.md#sharding.
enum class ShardingLayout {
  kConcatenated,  // Records are indexed as if the shards were concatenated.
  kInterleaved,   // Records are indexed round-robin across shards.
};

// When opening a bag, the LimitsStorage specifies whether the limits are read
// from disk or from RAM.
enum class LimitsStorage {
  kOnDisk,    // Limits are read from disk each time.
  kInMemory,  // Limits are copied from disk to RAM once and read from there.
};

// Writer only options.
class ExistingFileMode {
  // If the file already exists, the writer will start writing at this index.
  // If the file contains less than `start_index` records, the writer will fail
  // to open the file.
  size_t start_index = 0;
  bool opening_limits_placement_tail = false;
};

}  // namespace bagz

#endif  // BAGZ_SRC_BAGZ_OPTIONS_H_
