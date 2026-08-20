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
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/bagz_multi_index.h"
#include "src/bagz_reader.h"
#include "nanobind/nanobind.h"
#include "nanobind/stl/string.h"  // IWYU pragma: keep
#include "nanobind/stl/string_view.h"  // IWYU pragma: keep
#include "nanobind/stl/vector.h"  // IWYU pragma: keep

namespace bagz {
namespace {

namespace nb = nanobind;

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

void RegisterBagzMultiIndex(nb::module_& m) {
  nb::class_<BagzMultiIndex>(
      m, "MultiIndex",
      "An in-memory class for finding row-indices of record in Bag file.")
      .def(
          "__init__",
          [](BagzMultiIndex* t, const BagzReader& reader) {
            if (absl::StatusOr<BagzMultiIndex> index =
                    BagzMultiIndex::Create(reader);
                index.ok()) {
              new (t) BagzMultiIndex(*std::move(index));
            } else {
              throw std::invalid_argument(index.status().ToString());
            }
          },
          nb::arg("reader"), kInitDoc + 1,
          nb::call_guard<nb::gil_scoped_release>())
      .def(
          "get",
          [](const BagzMultiIndex& index, nb::object record_obj,
             nb::object def) -> nb::object {
            absl::string_view item;
            if (nb::isinstance<nb::bytes>(record_obj)) {
              nb::bytes b = nb::cast<nb::bytes>(record_obj);
              item = absl::string_view((const char*)b.data(), b.size());
            } else if (nb::isinstance<nb::str>(record_obj)) {
              item = nb::cast<std::string_view>(record_obj);
            } else {
              throw nb::type_error("item must be str or bytes");
            }
            std::optional<absl::Span<const size_t>> result;
            {
              nb::gil_scoped_release release;
              result = index[item];
            }
            if (result.has_value()) {
              return nb::cast(
                  std::vector<size_t>(result->begin(), result->end()));
            } else {
              return def;
            }
          },
          nb::arg("item"), nb::arg("default") = nb::none(), kGetDoc + 1)
      .def(
          "__getitem__",
          [](const BagzMultiIndex& index, nb::object record_obj) -> nb::object {
            absl::string_view item;
            if (nb::isinstance<nb::bytes>(record_obj)) {
              nb::bytes b = nb::cast<nb::bytes>(record_obj);
              item = absl::string_view((const char*)b.data(), b.size());
            } else if (nb::isinstance<nb::str>(record_obj)) {
              item = nb::cast<std::string_view>(record_obj);
            } else {
              throw nb::type_error("item must be str or bytes");
            }
            std::optional<absl::Span<const size_t>> result;
            {
              nb::gil_scoped_release release;
              result = index[item];
            }
            if (result.has_value()) {
              return nb::cast(
                  std::vector<size_t>(result->begin(), result->end()));
            } else {
              throw nb::key_error(std::string(item).c_str());
            }
          },
          kGetItemDoc + 1)
      .def(
          "__contains__",
          [](const BagzMultiIndex& index, nb::object record_obj) {
            absl::string_view record;
            if (nb::isinstance<nb::bytes>(record_obj)) {
              nb::bytes b = nb::cast<nb::bytes>(record_obj);
              record = absl::string_view((const char*)b.data(), b.size());
            } else if (nb::isinstance<nb::str>(record_obj)) {
              record = nb::cast<std::string_view>(record_obj);
            } else {
              throw nb::type_error("record must be str or bytes");
            }
            return index.Contains(record);
          },
          nb::arg("record"), kContainsDoc + 1)
      .def("__len__", &BagzMultiIndex::size, kLenDoc + 1);
}

}  // namespace bagz
