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

// Classes for converting between global indices and shard/indices. See
// also README.md#sharding for more information.
//
// Concatenating shards:
//
// Example if our Bagz-formatted files have the sizes [8, 4, 0, 5]. Then the
// `global_index` with `[0, 17)` will map to `shard` and `shard_index`:
//
//        | shard_index
//  shard |  0  1  2  3  4  5  6  7
//  ----- | -----------------------
//  0     |  0  1  2  3  4  5  6  7
//  1     |  8  9 10 11
//  2     |
//  3     | 12 13 14 15 16
//
// global_index | shard | shard_index
//  0           | 0     | 0
//  1           | 0     | 1
//  2           | 0     | 2
//  3           | 0     | 3
//  4           | 0     | 4
//  5           | 0     | 5
//  6           | 0     | 6
//  7           | 0     | 7
//  8           | 1     | 0
//  9           | 1     | 1
// 10           | 1     | 2
// 11           | 1     | 3
// 12           | 3     | 0
// 13           | 3     | 1
// 14           | 3     | 2
// 15           | 3     | 3
// 16           | 3     | 4
//
//
// Interleaved sharding:
//
// In interleaved sharding, we assign the global_index in round robin fashion
// to each shard.
//
// Example if 3 Bagz-formatted files have a total of 17 records. Then the
// `global_index` with `[0, 17)` will map to `shard` and `shard_index`:
//
//        | shard_index
//  shard |  0  1  2  3  4  5
//  ----- | -----------------------
//  0     |  0  3  6  9 12 15
//  1     |  1  4  7 10 13 16
//  2     |  2  5  8 11 14
//
// global_index | shard | shard_index
//  0           | 0     | 0
//  1           | 1     | 0
//  2           | 2     | 0
//  3           | 0     | 1
//  4           | 1     | 1
//  5           | 2     | 1
//  6           | 0     | 2
//  7           | 1     | 2
//  8           | 2     | 2
//  9           | 0     | 3
// 10           | 1     | 3
// 11           | 2     | 3
// 12           | 0     | 4
// 13           | 1     | 4
// 14           | 2     | 4
// 15           | 0     | 5
// 16           | 1     | 5

#ifndef BAGZ_SRC_INTERNAL_SHARD_INDEXING_H_
#define BAGZ_SRC_INTERNAL_SHARD_INDEXING_H_

#include <cstddef>
#include <utility>
#include <vector>

namespace bagz::internal {

// The shard and shard_index of a global index.
struct ShardIndex {
  size_t shard;
  size_t shard_index;
};

// This represents a `slice` of global indices in a shard.
struct ShardRange {
  size_t shard;
  size_t shard_start;
  size_t count;

  // The offset and stride (slice) in the global index range provided.
  size_t result_offset;
  size_t result_stride;
};

// Calculates the shard/indices of items given a global indices.
class ShardIndexing {
 public:
  // Creates a ShardIndexing instance for converting global indices to
  // shard/indices. `accumulated_count` is the accumulated number of items in
  // each shard. `is_interleaved` indicates whether the sharding is interleaved
  // or concatenated.
  explicit ShardIndexing(std::vector<size_t> accumulated_count,
                         bool is_interleaved)
      : accumulated_count_(std::move(accumulated_count)),
        is_interleaved_(is_interleaved) {}

  // Total number of items in all shards.
  size_t size() const {
    return accumulated_count_.empty() ? 0 : accumulated_count_.back();
  }

  // Returns whether global_index_start and count forms a valid range.
  bool valid_range(size_t global_index_start, size_t count) const {
    size_t max_count = size();
    // The order of checks matters to avoids overflow.
    return count <= max_count && global_index_start <= max_count - count;
  }

  // Converts an `global_index` to a ShardIndex.
  // Precondition: `global_index < size()`.
  ShardIndex index(size_t global_index) const;

  // Spits the range into ShardRange where each ShardRange represents a slice
  // of global indices in a shard.
  // Precondition: `valid_range(global_index_start, count)`
  std::vector<ShardRange> range(size_t global_index_start, size_t count) const;

 private:
  std::vector<size_t> accumulated_count_;
  bool is_interleaved_;
};

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_SHARD_INDEXING_H_
