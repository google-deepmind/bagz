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

#ifndef BAGZ_SRC_INTERNAL_SERIALIZE_H_
#define BAGZ_SRC_INTERNAL_SERIALIZE_H_

#include <bit>
#include <cstdint>

namespace bagz::internal {

// Converts to/from big-endian and little-endian when on big-endian machines.
constexpr inline uint64_t SerializeUint64(uint64_t serialise) {
  if constexpr (std::endian::native == std::endian::big) {
    // Tested - becomes bswap64(serialise);
    return (((serialise & 0x00000000000000FFull) << 56) |
            ((serialise & 0x000000000000FF00ull) << 40) |
            ((serialise & 0x0000000000FF0000ull) << 24) |
            ((serialise & 0x00000000FF000000ull) << 8) |
            ((serialise & 0x000000FF00000000ull) >> 8) |
            ((serialise & 0x0000FF0000000000ull) >> 24) |
            ((serialise & 0x00FF000000000000ull) >> 40) |
            ((serialise & 0xFF00000000000000ull) >> 56));
  } else {
    return serialise;
  }
}

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_SERIALIZE_H_
