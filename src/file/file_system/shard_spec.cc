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

#include "src/file/file_system/shard_spec.h"

#include <cctype>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace bagz {

namespace {

std::string FormatShard(absl::string_view prefix, int shard, int num_shards,
                        absl::string_view suffix) {
  return absl::StrFormat("%s-%05d-of-%05d%s", prefix, shard, num_shards,
                         suffix);
}

struct ShardSpec {
  absl::string_view prefix;
  int num_shards;
  absl::string_view suffix;
};

std::optional<ShardSpec> ParseShardSpec(absl::string_view spec) {
  auto start_pos = spec.find_last_of('/');
  if (start_pos == absl::string_view::npos) {
    start_pos = 0;
  }
  start_pos = spec.find('@', start_pos);
  if (start_pos == absl::string_view::npos) {
    return std::nullopt;
  }
  int num_shards = 0;
  auto char_pos = start_pos + 1;
  for (; char_pos < spec.size(); ++char_pos) {
    unsigned char c = spec[char_pos];
    if (std::isdigit(c)) {
      num_shards *= 10;
      num_shards += c - '0';
      if (num_shards >= 100'000) {
        return std::nullopt;
      }
    } else {
      break;
    }
  }
  if (num_shards == 0) {
    return std::nullopt;
  }
  return ShardSpec{.prefix = spec.substr(0, start_pos),
                   .num_shards = num_shards,
                   .suffix = spec.substr(char_pos)};
}

// Replaces @N with expanded shard names.
// <path>/<name>@N.bag -> <path>/<name>-00000-of-NNNNN.bag, ...
// Does not support wildcards.
std::vector<std::string> ExpandAtSpec(absl::string_view spec) {
  std::optional<ShardSpec> shard_spec = ParseShardSpec(spec);
  if (!shard_spec.has_value()) {
    return {std::string(spec)};
  }
  std::vector<std::string> result;
  result.reserve(shard_spec->num_shards);
  for (int i = 0; i < shard_spec->num_shards; ++i) {
    result.push_back(FormatShard(shard_spec->prefix, i, shard_spec->num_shards,
                                 shard_spec->suffix));
  }
  return result;
}

}  // namespace

// Splits comma-separated list of file specs into individual file specs.
std::vector<std::string> ExpandShardSpec(absl::string_view shard_spec) {
  std::vector<std::string> result;
  for (absl::string_view spec :
       absl::StrSplit(shard_spec, ',', absl::SkipEmpty())) {
    std::vector<std::string> range = ExpandAtSpec(spec);
    result.insert(result.end(), std::make_move_iterator(range.begin()),
                  std::make_move_iterator(range.end()));
  }
  return result;
}

std::string CanonicaliseShardSpec(
    absl::string_view shard_spec,
    absl::FunctionRef<std::string(const std::string&)> glob) {
  if (absl::string_view::size_type pos = shard_spec.rfind("@*");
      pos != absl::string_view::npos) {
    absl::string_view prefix = shard_spec.substr(0, pos);
    absl::string_view suffix = shard_spec.substr(pos + 2);
    std::string pattern =
        absl::StrCat(prefix, "-00000-of-[0-9][0-9][0-9][0-9][0-9]", suffix);

    std::string glob_result = glob(pattern);
    // The result should be of the form:
    // <path>/<name>-00000-of-NNNNN<suffix>
    // We make sure that we can extract the NNNNN part.
    if (glob_result.size() >= 5 + suffix.size()) {
      absl::string_view result = glob_result;
      size_t offset = glob_result.size() - 5 - suffix.size();
      absl::string_view shard_count = result.substr(offset, 5);
      auto leading_zeroes = shard_count.find_first_not_of('0');
      if (leading_zeroes != absl::string_view::npos) {
        return absl::StrCat(prefix, "@", shard_count.substr(leading_zeroes),
                            suffix);
      }
    }
  }
  return std::string(shard_spec);
}

}  // namespace bagz
