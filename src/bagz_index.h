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

// An index from the Bagz value to its index in the Bagz file.

#ifndef BAGZ_SRC_BAGZ_INDEX_H_
#define BAGZ_SRC_BAGZ_INDEX_H_

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/bagz_reader.h"

namespace bagz {

// Creates a map from record to row-index.
class BagzIndex {
 public:
  // Reads entire bag into an associative container. Duplicate keys return first
  // index read. Compare `reader.size()` with this->size()` to detect duplicate
  // keys.
  static absl::StatusOr<BagzIndex> Create(const BagzReader& reader);

  // Returns row-index associated with record.
  std::optional<size_t> operator[](absl::string_view record) const {
    if (auto it = index_.find(record); it != index_.end()) {
      return it->second;
    } else {
      return std::nullopt;
    }
  }

  // Returns whether record is in index.
  bool Contains(absl::string_view record) const {
    return index_.contains(record);
  }

  // Returns number of unique records in container.
  size_t size() const { return index_.size(); }

 private:
  BagzIndex(absl::flat_hash_map<std::string, size_t> index)
      : index_(std::move(index)) {}
  absl::flat_hash_map<std::string, size_t> index_;
};

}  // namespace bagz

#endif  // BAGZ_SRC_BAGZ_INDEX_H_
