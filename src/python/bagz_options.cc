// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/python/bagz_options.h"

#include <string>
#include <utility>

#include "src/bagz_options.h"
#include "nanobind/nanobind.h"
#include "nanobind/stl/string.h"  // IWYU pragma: keep

namespace bagz {

namespace {

namespace nb = nanobind;

constexpr char kCompressionZtdInitDoc[] = R"(
Creates a CompressionZstd.

Args:
  level: The compression level.
  dictionary: The dictionary to use. (Empty string to not use a dictionary.)
)";

constexpr char kShardingLayoutEnumDoc[] = R"(
When opening more than one shard for a bag, the shard type specifies how
the records in each shard are indexed. See README.md#sharding.)";

}  // namespace

void RegisterBagzOptions(nb::module_& m) {
  nb::enum_<LimitsPlacement>(
      m, "LimitsPlacement",
      "Whether limits are at the end of the file or in a separate file.")
      .value("TAIL", LimitsPlacement::kTail,
             "Place limits at the end of the file")
      .value("SEPARATE", LimitsPlacement::kSeparate,
             "Place limits in a separate file");

  nb::enum_<LimitsStorage>(m, "LimitsStorage",
                           "Whether to read the limits from disk for every "
                           "read or to cache the limits in memory.")
      .value("ON_DISK", LimitsStorage::kOnDisk,
             "Limits are read from disk each time")
      .value("IN_MEMORY", LimitsStorage::kInMemory,
             "Limits are copied from disk to RAM once and read from there");

  nb::class_<CompressionNone>(m, "CompressionNone",
                              "Override the default compression to be none.")
      .def("__init__", [](CompressionNone* t) { new (t) CompressionNone{}; })
      .def("__getstate__",
           [](const CompressionNone&) { return nb::make_tuple(); })
      .def("__setstate__",
           [](CompressionNone* t, nb::tuple) { new (t) CompressionNone{}; });

  nb::class_<CompressionAutoDetect>(
      m, "CompressionAutoDetect",
      "Use the default compression for the filename extension.")
      .def("__init__",
           [](CompressionAutoDetect* t) { new (t) CompressionAutoDetect{}; })
      .def("__getstate__",
           [](const CompressionAutoDetect&) { return nb::make_tuple(); })
      .def("__setstate__", [](CompressionAutoDetect* t, nb::tuple) {
        new (t) CompressionAutoDetect{};
      });

  nb::class_<CompressionZstd>(m, "CompressionZstd", "Use Zstd compression.")
      .def(
          "__init__",
          [](CompressionZstd* t, int level, std::string dictionary) {
            new (t) CompressionZstd{.dictionary = std::move(dictionary),
                                    .level = level};
          },
          nb::arg("level") = 0, nb::arg("dictionary") = "",
          kCompressionZtdInitDoc + 1)
      .def_rw("level", &CompressionZstd::level)
      .def_rw("dictionary", &CompressionZstd::dictionary)
      .def("__getstate__",
           [](const CompressionZstd& c) {
             return nb::make_tuple(c.level, c.dictionary);
           })
      .def("__setstate__", [](CompressionZstd* t, nb::tuple state) {
        new (t) CompressionZstd{
            .dictionary = nb::cast<std::string>(state[1]),
            .level = nb::cast<int>(state[0]),
        };
      });

  nb::enum_<ShardingLayout>(m, "ShardingLayout", kShardingLayoutEnumDoc + 1)
      .value("CONCATENATED", ShardingLayout::kConcatenated,
             "Concatenated sharding")
      .value("INTERLEAVED", ShardingLayout::kInterleaved,
             "Interleaved sharding");
}

}  // namespace bagz
