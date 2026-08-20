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

#include "src/python/bagz_writer.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/bagz_options.h"
#include "src/bagz_writer.h"
#include "nanobind/nanobind.h"
#include "nanobind/stl/string.h"  // IWYU pragma: keep
#include "nanobind/stl/string_view.h"  // IWYU pragma: keep
#include "nanobind/stl/variant.h"  // IWYU pragma: keep

namespace bagz {

namespace {

namespace nb = nanobind;

const char kWriterInitDoc[] = R"(
Open a single Bagz file shard for writing.

Use as a context manager to ensure the file is closed.

Example:

```python
with bagz.Writer(filename) as writer:
  for record in records:
    writer.write(record)
```

Args:
  filename: Filename to open for writing. During writing, a limits file will be
    created with the same name as the filename with the prefix "limits.".
  options: See `bagz.Writer.Options`.
)";

constexpr char kWriterWriteDoc[] = R"(
Writes a record to the Bagz file.

Compresses according to the `compression` option. Writes may be buffered but can
be flushed with `flush`.

Args:
  record: the record to write.
)";

constexpr char kWriterFlushDoc[] = R"(
Flushes the BagzWriter.

Calls `Flush` on the 'records' and 'limits'. When completed, data written so far
will be available to be read using `bagz.Reader`.

Throws an error either if the 'records' or 'limits' FileWriters fail to flush.
)";

constexpr char kWriterCloseDoc[] = R"(
Closes the BagzWriter.

When created with `options.limits_placement`

* `LimitsPlacement.SEPARATE` - 'limits' and 'records' are closed.
* `LimitsPlacement.TAIL` - the 'limits' are written to the end of 'records'
  and deleted. 'records' is closed.

Throws an error if any of the file operations fail. The data that was
successfully written will be recoverable using `bagz.Reader` regardless of
the `limits` placement.
)";

constexpr char kWriterOptionsDoc[] = R"(
Options for creating the bagz.Writer.

Attributes:
  limits_placement: Placement of the limits section on close defaulting to
    TAIL.
  compression: Compression algorithm to use defaulting to auto-detection.
)";

constexpr char kWriterOptionsInitDoc[] = R"(
Creates a `bagz.Writer.Options`.

Args:
  limits_placement: Placement of the limits section on close defaulting to TAIL.
  compression: Compression algorithm to use defaulting to auto-detection.
)";

}  // namespace

void RegisterBagzWriter(nb::module_& m) {
  auto writer =
      nb::class_<BagzWriter>(m, "Writer", "Writes a single Bagz shard.");

  nb::class_<BagzWriter::Options>(writer, "Options", kWriterOptionsDoc + 1)
      .def(
          "__init__",
          [](BagzWriter::Options* t, LimitsPlacement limits_placement,
             Compression compression) {
            new (t) BagzWriter::Options{
                .limits_placement = limits_placement,
                .compression = std::move(compression),
            };
          },
          nb::arg("limits_placement") = BagzWriter::Options{}.limits_placement,
          nb::arg("compression") = BagzWriter::Options{}.compression,
          kWriterOptionsInitDoc + 1)
      .def_rw("limits_placement", &BagzWriter::Options::limits_placement)
      .def_rw("compression", &BagzWriter::Options::compression);

  writer
      .def(
          "__init__",
          [](BagzWriter* t, nb::object filename_obj,
             const BagzWriter::Options& options) {
            static absl::NoDestructor<nb::object> fspath(
                nb::module_::import_("os").attr("fspath"));
            std::string filename =
                nb::cast<std::string>((*fspath)(filename_obj));
            {
              nb::gil_scoped_release release_gil;
              absl::StatusOr<BagzWriter> writer =
                  BagzWriter::OpenFile(filename, options);
              if (!writer.ok()) {
                throw std::invalid_argument(writer.status().ToString());
              }
              new (t) BagzWriter(*std::move(writer));
            }
          },
          nb::arg("filename"), nb::arg("options") = BagzWriter::Options(),
          kWriterInitDoc + 1)
      .def("__enter__", [](nb::handle self) { return self; })
      .def(
          "__exit__",
          [](nb::handle self, nb::handle exc_type, nb::handle exc_value,
             nb::handle traceback) {
            BagzWriter* writer = nb::inst_ptr<BagzWriter>(self);
            absl::Status status = writer->Close();
            if (!status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          nb::arg("exc_type").none(), nb::arg("exc_value").none(),
          nb::arg("traceback").none(), nb::call_guard<nb::gil_scoped_release>())
      .def(
          "write",
          [](BagzWriter* writer, nb::object record_obj) {
            absl::string_view record;
            if (nb::isinstance<nb::bytes>(record_obj)) {
              nb::bytes b = nb::cast<nb::bytes>(record_obj);
              record = absl::string_view((const char*)b.data(), b.size());
            } else if (nb::isinstance<nb::str>(record_obj)) {
              record = nb::cast<std::string_view>(record_obj);
            } else {
              throw nb::type_error("record must be str or bytes");
            }
            nb::gil_scoped_release release;
            if (absl::Status status = writer->Write(record); !status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          nb::lock_self(), nb::arg("record"), kWriterWriteDoc + 1)
      .def(
          "close",
          [](BagzWriter* writer) {
            if (absl::Status status = writer->Close(); !status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          nb::lock_self(), nb::call_guard<nb::gil_scoped_release>(),
          kWriterCloseDoc + 1)
      .def(
          "flush",
          [](BagzWriter* writer) {
            if (absl::Status status = writer->Flush(); !status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          nb::lock_self(), nb::call_guard<nb::gil_scoped_release>(),
          kWriterFlushDoc + 1);
}

}  // namespace bagz
