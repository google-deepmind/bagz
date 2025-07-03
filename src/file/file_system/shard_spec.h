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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_SHARD_SPEC_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_SHARD_SPEC_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

namespace bagz {

// Splits shard-spec into individual shard names.
//
// A shard spec is a comma-separated list of paths where each path may contain
// a sharding specifier "@N", where N is an integer in the range [1, 99999].
// When a sharding specifier is present, it is replaced with a range of
// individual shard names:
//
//   path@N -> path-00000-of-NNNNN, path-00001-of-NNNNN, ...
//
// For example:
//
//   path@3 -> path-00000-of-00003, path-00001-of-00003, path-00002-of-00003
//
// The replacement keeps the original order of the shards.
// For example the following:
//
//   "foo@3.path,bar@1.path,no_shards.path,baz@13.path"
//
// expands to:
//
//   "foo-00000-of-00003.path",
//   "foo-00001-of-00003.path",
//   "foo-00002-of-00003.path",
//   "bar-00000-of-00001.path",
//   "no_shards.path",
//   "baz-00000-of-00013.path",
//   "baz-00001-of-00013.path",
//   ...
//   "baz-00012-of-00013.path",
//
// Invalid sharding specifiers are not split and left as-is. This includes:
//   1) N outside of [1, 99999]. Examples: "foo@0.path", "foo@100000.path".
//   2) @N before slash. Example: "foo@10/bar.path".
//   3) N is '*' or '?'. Example: "foo@*.path", "foo@?.path".
//
std::vector<std::string> ExpandShardSpec(absl::string_view shard_spec);

// Canonicalises `shard_spec` by replacing `@*` with the number of shards.
// If the shard spec does not contain `@*`, `shard_spec` is returned unchanged.
//
// The canonicalisation is done by calling `glob` with a pattern that matches
// the shard spec. The pattern is of the form:
//
//   <path>/<name>-00000-of-[0-9][0-9][0-9][0-9][0-9]<suffix>
//
// If multiple files match the pattern, then `glob` should return the last match
// lexicographically.
// If the `glob` returns a result, the returned name is used to determine
// number of shards. If the glob returns an empty string, the original
// `shard_spec` is returned.
//
// For example, if the glob returns "foo-00000-of-00003.suffix", the shard spec
// "foo@*.suffix" is canonicalised to "foo@3.suffix". If the glob returns
// nothing, the shard spec "foo@*.suffix" is returned unchanged.
std::string CanonicaliseShardSpec(
    absl::string_view shard_spec,
    absl::FunctionRef<std::string(const std::string&)> glob);

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_SHARD_SPEC_H_
