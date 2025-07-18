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

#include "src/python/bagz_multi_index.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/bagz_multi_index.h"
#include "src/bagz_reader.h"
#include "pybind11/cast.h"
#include "pybind11/gil.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

namespace bagz {
namespace {

namespace py = pybind11;

constexpr char kInitDoc[] = R"(
Creates a reverse index of record to record-index.

Args:
  reader: the bag reader to read the records from.
)";

constexpr char kGetDoc[] = R"(
Returns all row-indices of `record` or None if `record` is not found.

Args:
  record: the record to get the index of.
)";

constexpr char kGetItemDoc[] = R"(
Returns all row-indices of `record`.

Args:
  record: the record to get the index of.

Raises:
  KeyError: if the record is not found.
)";

constexpr char kContainsDoc[] = R"(
Returns whether record is in index.

Args:
  record: the record to lookup.
)";

constexpr char kLenDoc[] = R"(
Returns the number of unique records.

Compare with len(bag) to detect duplicates.
)";

}  // namespace

void RegisterBagzMultiIndex(py::module& m) {
  py::class_<BagzMultiIndex>(
      m, "MultiIndex",
      "An in-memory class for finding row-indices of record in Bag file.")
      .def(py::init([](const BagzReader& reader) {
             if (absl::StatusOr<BagzMultiIndex> index =
                     BagzMultiIndex::Create(reader);
                 index.ok()) {
               return *std::move(index);
             } else {
               throw std::invalid_argument(index.status().ToString());
             }
           }),
           py::arg("reader"), py::doc(kInitDoc + 1),
           py::call_guard<py::gil_scoped_release>())
      .def(
          "get",
          [](const BagzMultiIndex& index, absl::string_view item,
             py::object def) -> py::object {
            std::optional<absl::Span<const size_t>> result;
            {
              py::gil_scoped_release release;
              result = index[item];
            }
            if (result.has_value()) {
              py::list l(result->size());
              for (size_t i = 0; i < result->size(); ++i) {
                l[i] = (*result)[i];
              }
              return l;
            } else {
              return def;
            }
          },
          py::arg("item"), py::arg("default") = py::none(),
          py::doc(kGetDoc + 1))
      .def(
          "__getitem__",
          [](const BagzMultiIndex& index, absl::string_view item) {
            std::optional<absl::Span<const size_t>> result;
            {
              py::gil_scoped_release release;
              result = index[item];
            }
            if (result.has_value()) {
              py::list l(result->size());
              for (size_t i = 0; i < result->size(); ++i) {
                l[i] = (*result)[i];
              }
              return l;
            } else {
              throw py::key_error(std::string(item));
            }
          },
          py::doc(kGetItemDoc + 1))
      .def("__contains__", &BagzMultiIndex::Contains, py::arg("record"),
           py::doc(kContainsDoc + 1))
      .def("__len__", &BagzMultiIndex::size, py::doc(kLenDoc + 1));
}

}  // namespace bagz
