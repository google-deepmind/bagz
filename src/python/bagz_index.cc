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

#include "src/bagz_index.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/bagz_reader.h"
#include "nanobind/nanobind.h"
#include "nanobind/stl/optional.h"  // IWYU pragma: keep
#include "nanobind/stl/string.h"  // IWYU pragma: keep
#include "nanobind/stl/string_view.h"  // IWYU pragma: keep

namespace bagz {
namespace {

namespace nb = nanobind;

constexpr char kInitDoc[] = R"(
Creates a reverse index of record to record-index.

Args:
  reader: the bag reader to read the records from.
)";

constexpr char kGetDoc[] = R"(
Returns first row-index of `record` or None if `record` is not found.

Args:
  record: the record to get the index of.
)";

constexpr char kGetItemDoc[] = R"(
Returns first row-index of `record`.

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

void RegisterBagzIndex(nb::module_& m) {
  nb::class_<BagzIndex>(
      m, "Index",
      "An in-memory class for finding row-index of record in Bag file.")
      .def(
          "__init__",
          [](BagzIndex* t, const BagzReader& reader) {
            if (absl::StatusOr<BagzIndex> index = BagzIndex::Create(reader);
                index.ok()) {
              new (t) BagzIndex(*std::move(index));
            } else {
              throw std::invalid_argument(index.status().ToString());
            }
          },
          nb::arg("reader"), kInitDoc + 1,
          nb::call_guard<nb::gil_scoped_release>())
      .def(
          "get",
          [](const BagzIndex& index,
             nb::object record_obj) -> std::optional<size_t> {
            absl::string_view record;
            if (nb::isinstance<nb::bytes>(record_obj)) {
              nb::bytes b = nb::cast<nb::bytes>(record_obj);
              record = absl::string_view((const char*)b.data(), b.size());
            } else if (nb::isinstance<nb::str>(record_obj)) {
              record = nb::cast<std::string_view>(record_obj);
            } else {
              throw nb::type_error("record must be str or bytes");
            }
            return index[record];
          },
          nb::arg("record"), kGetDoc + 1)
      .def(
          "__getitem__",
          [](const BagzIndex& index, nb::object record_obj) {
            absl::string_view record;
            if (nb::isinstance<nb::bytes>(record_obj)) {
              nb::bytes b = nb::cast<nb::bytes>(record_obj);
              record = absl::string_view((const char*)b.data(), b.size());
            } else if (nb::isinstance<nb::str>(record_obj)) {
              record = nb::cast<std::string_view>(record_obj);
            } else {
              throw nb::type_error("record must be str or bytes");
            }
            if (std::optional<size_t> i = index[record]; i.has_value()) {
              return *i;
            } else {
              throw nb::key_error(std::string(record).c_str());
            }
          },
          kGetItemDoc + 1)
      .def(
          "__contains__",
          [](const BagzIndex& index, nb::object record_obj) {
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
      .def("__len__", &BagzIndex::size, kLenDoc + 1);
}

}  // namespace bagz
