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

#include "src/bagz_index.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "src/bagz_reader.h"

namespace bagz {

absl::StatusOr<BagzIndex> BagzIndex::Create(const BagzReader& reader) {
  size_t num_records = reader.size();
  absl::StatusOr<std::vector<std::string>> records =
          reader.ReadRange(0, num_records);
  if (!records.ok()) {
    return records.status();
  }

  absl::flat_hash_map<std::string, size_t> index;
  index.reserve(num_records);
  for (size_t i = 0; i < num_records; ++i) {
    index.emplace(std::move((*records)[i]), i);
  }
  return BagzIndex(std::move(index));
}

}  // namespace bagz
