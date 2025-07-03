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

#include "absl/strings/string_view.h"
#include "src/bagz_options.h"
#include "pybind11/cast.h"
#include "pybind11/gil.h"
#include "pybind11/numpy.h"
#include "pybind11/pybind11.h"
#include "pybind11/pytypes.h"
#include "pybind11/stl.h"

namespace bagz {

namespace {

namespace py = pybind11;

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

void RegisterBagzOptions(py::module& m) {
  py::enum_<LimitsPlacement>(
      m, "LimitsPlacement",
      "Whether limits are at the end of the file or in a separate file.")
      .value("TAIL", LimitsPlacement::kTail,
             "Place limits at the end of the file")
      .value("SEPARATE", LimitsPlacement::kSeparate,
             "Place limits in a separate file");

  py::enum_<LimitsStorage>(m, "LimitsStorage",
                           "Whether to read the limits from disk for every "
                           "read or to cache the limits in memory.")
      .value("ON_DISK", LimitsStorage::kOnDisk,
             "Limits are read from disk each time")
      .value("IN_MEMORY", LimitsStorage::kInMemory,
             "Limits are copied from disk to RAM once and read from there");

  py::class_<CompressionNone>(m, "CompressionNone",
                              "Override the default compression to be none.")
      .def(py::init([]() { return CompressionNone{}; }),
           py::return_value_policy::move);

  py::class_<CompressionAutoDetect>(
      m, "CompressionAutoDetect",
      "Use the default compression for the filename extension.")
      .def(py::init([]() { return CompressionAutoDetect{}; }),
           py::return_value_policy::move);

  py::class_<CompressionZstd>(m, "CompressionZstd", "Use Zstd compression.")
      .def(py::init([](int level, absl::string_view dictionary) {
             return CompressionZstd{.dictionary = std::string(dictionary),
                                    .level = level};
           }),
           py::arg("level") = 0, py::arg("dictionary") = "",
           py::return_value_policy::move, py::doc(kCompressionZtdInitDoc + 1))
      .def_readwrite("level", &CompressionZstd::level)
      .def_readwrite("dictionary", &CompressionZstd::dictionary);

  py::enum_<ShardingLayout>(m, "ShardingLayout", kShardingLayoutEnumDoc + 1)
      .value("CONCATENATED", ShardingLayout::kConcatenated,
             "Concatenated sharding")
      .value("INTERLEAVED", ShardingLayout::kInterleaved,
             "Interleaved sharding");
}

}  // namespace bagz
