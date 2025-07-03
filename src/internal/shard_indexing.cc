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

#include "src/internal/shard_indexing.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "absl/types/span.h"

namespace bagz::internal {
namespace {

// Returns the number of items in the given shard.
// `shard` must be < `accumulated_count.size()`.
size_t NumRecordsInShard(size_t shard,
                         absl::Span<const size_t> accumulated_count) {
  return shard == 0 ? accumulated_count[0]
                    : accumulated_count[shard] - accumulated_count[shard - 1];
}

// Concatenated: Indexing is as if the shards were concatenated.
//        | shard_index
//  shard |  0  1  2  3  4  5  6  7
//  ----- | -----------------------
//  0     |  0  1  2  3  4  5  6  7
//  1     |  8  9 10 11
//  2     |
//  3     | 12 13 14 15 16
//
// So index 13 -> shard = 3, shard_index = 1.
ShardIndex ShardIndexFromConcatated(absl::Span<const size_t> accumulated_count,
                                    size_t global_index) {
  // Pick the first accumulated_num_item that has a value greater than the
  // index.
  auto it = std::lower_bound(accumulated_count.begin(), accumulated_count.end(),
                             global_index + 1);
  size_t shard = it - accumulated_count.begin();
  size_t shard_index = global_index;
  if (it != accumulated_count.begin()) {
    // Remove previous accumulated_num_item from the index.
    --it;
    shard_index -= *it;
  }
  return ShardIndex{.shard = shard, .shard_index = shard_index};
}

// Interleaved: Indexing round robins between the shards.
//        | shard_index
//  shard |  0  1  2  3  4  5
//  ----- | -----------------------
//  0     |  0  3  6  9 12 15
//  1     |  1  4  7 10 13 16
//  2     |  2  5  8 11 14
// So index 13 -> shard = 1, shard_index = 4.
ShardIndex ShardIndexFromInterleaved(absl::Span<const size_t> accumulated_count,
                                     size_t global_index) {
  const size_t shard_index = global_index / accumulated_count.size();
  const size_t shard = global_index % accumulated_count.size();
  return ShardIndex{.shard = shard, .shard_index = shard_index};
}

// With shard sizes: [8, 4, 0, 5]
//
// global_index | shard | shard_index
//  0           | 0     | 0
//  1           | 0     | 1
//  2           | 0     | 2
//  3           | 0     | 3
//  4           | 0     | 4
//  5           | 0     | 5 <--- global_index_start
//  6           | 0     | 6
//  7           | 0     | 7
//  8           | 1     | 0
//  9           | 1     | 1
// 10           | 1     | 2
// 11           | 1     | 3
// 12           | 3     | 0
// 13           | 3     | 1
// 14           | 3     | 2
// 15           | 3     | 3 <--- global_index_start + count
// 16           | 3     | 4
// For concatenated shard_stride is always 1.
// So calling with global_index_start = 5, count = 10 ->
//   {shard = 0, shard_start = 5, count = 3, result_offset = 0}
//   {shard = 1, shard_start = 0, count = 4, result_offset = 3}
//   {shard = 3, shard_start = 0, count = 3, result_offset = 7}
std::vector<ShardRange> ShardRangeFromConcatated(
    absl::Span<const size_t> accumulated_count, size_t global_index_start,
    size_t count) {
  if (count == 0) {
    return {};
  }
  ShardIndex first_shard_index =
      ShardIndexFromConcatated(accumulated_count, global_index_start);
  ShardIndex last_shard_index = ShardIndexFromConcatated(
      accumulated_count, global_index_start + count - 1);
  std::vector<ShardRange> ranges;
  ranges.reserve(last_shard_index.shard - first_shard_index.shard + 1);
  size_t result_offset = 0;
  for (size_t i = first_shard_index.shard; i <= last_shard_index.shard; ++i) {
    size_t shard_start =
        i == first_shard_index.shard ? first_shard_index.shard_index : 0;
    size_t shard_end = i == last_shard_index.shard
                           ? last_shard_index.shard_index + 1
                           : NumRecordsInShard(i, accumulated_count);
    size_t shard_count = shard_end - shard_start;
    if (shard_count) {
      ranges.push_back(ShardRange{.shard = i,
                                  .shard_start = shard_start,
                                  .count = shard_count,
                                  .result_offset = result_offset,
                                  .result_stride = 1});
    }
    result_offset += shard_count;
  }
  return ranges;
}

// Returns the shard ranges for the given range of items.
//
// With three shards it looks like this.
// global_index | shard | shard_index
//  0           | 0     | 0
//  1           | 1     | 0
//  2           | 2     | 0
//  3           | 0     | 1
//  4           | 1     | 1
//  5           | 2     | 1 <--- global_index_start
//  6           | 0     | 2
//  7           | 1     | 2
//  8           | 2     | 2
//  9           | 0     | 3
// 10           | 1     | 3
// 11           | 2     | 3
// 12           | 0     | 4
// 13           | 1     | 4
// 14           | 2     | 4
// 15           | 0     | 5 <--- global_index_start + count
// 16           | 1     | 5
//
// For interleaved `shard_stride` is always accumulated_count.size().
// So calling with global_index_start = 5, count = 10 ->
//   {shard = 2, shard_start = 1, count = 4, result_offset = 0}
//   {shard = 0, shard_start = 2, count = 3, result_offset = 1}
//   {shard = 1, shard_start = 0, count = 3, result_offset = 2}
std::vector<ShardRange> ShardRangeFromInterleaved(
    absl::Span<const size_t> accumulated_count, size_t global_index_start,
    size_t count) {
  size_t num_shards_required = std::min(count, accumulated_count.size());
  std::vector<ShardRange> ranges;
  ranges.reserve(num_shards_required);
  ShardIndex shard_index =
      ShardIndexFromInterleaved(accumulated_count, global_index_start);
  size_t span = accumulated_count.size();
  for (size_t i = 0; i < num_shards_required; ++i) {
    size_t shard_id = i + shard_index.shard;
    const bool new_start = shard_id >= span;
    if (new_start) {
      shard_id -= span;
    }
    const size_t index_start = shard_index.shard_index + (new_start ? 1 : 0);
    const size_t index_end_global =
        (global_index_start + count - shard_id + span - 1);
    const size_t index_end = index_end_global / span;
    ranges.push_back(ShardRange{.shard = shard_id,
                                .shard_start = index_start,
                                .count = index_end - index_start,
                                .result_offset = i,
                                .result_stride = span});
  }
  return ranges;
}

}  // namespace

ShardIndex ShardIndexing::index(size_t global_index) const {
  if (is_interleaved_) {
    return ShardIndexFromInterleaved(accumulated_count_, global_index);
  } else {
    return ShardIndexFromConcatated(accumulated_count_, global_index);
  }
}

std::vector<ShardRange> ShardIndexing::range(size_t global_index_start,
                                             size_t count) const {
  if (is_interleaved_) {
    return ShardRangeFromInterleaved(accumulated_count_, global_index_start,
                                     count);
  } else {
    return ShardRangeFromConcatated(accumulated_count_, global_index_start,
                                    count);
  }
}

}  // namespace bagz::internal
