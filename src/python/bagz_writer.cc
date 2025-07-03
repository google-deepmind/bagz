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
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/bagz_options.h"
#include "src/bagz_writer.h"
#include "pybind11/attr.h"
#include "pybind11/cast.h"
#include "pybind11/gil.h"
#include "pybind11/numpy.h"
#include "pybind11/pybind11.h"
#include "pybind11/pytypes.h"
#include "pybind11/stl.h"

namespace bagz {

namespace {

namespace py = pybind11;

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

void RegisterBagzWriter(pybind11::module& m) {
  auto writer =
      py::class_<BagzWriter>(m, "Writer", "Writes a single Bagz shard.");

  py::class_<BagzWriter::Options>(writer, "Options", kWriterOptionsDoc + 1)
      .def(py::init(
               [](LimitsPlacement limits_placement, Compression compression) {
                 return BagzWriter::Options{
                     .limits_placement = limits_placement,
                     .compression = std::move(compression),
                 };
               }),
           py::arg("limits_placement") = BagzWriter::Options{}.limits_placement,
           py::arg("compression") = BagzWriter::Options{}.compression,
           py::doc(kWriterOptionsInitDoc + 1))
      .def_readwrite("limits_placement", &BagzWriter::Options::limits_placement)
      .def_readwrite("compression", &BagzWriter::Options::compression);

  writer
      .def(py::init(
               [](py::object filename_obj, const BagzWriter::Options& options) {
                 static absl::NoDestructor<py::object> fspath(
                     py::module::import("os").attr("fspath"));
                 std::string filename =
                     py::cast<std::string>((*fspath)(filename_obj));
                 {
                   py::gil_scoped_release release_gil;
                   absl::StatusOr<BagzWriter> writer =
                       BagzWriter::OpenFile(filename, options);
                   if (!writer.ok()) {
                     throw std::invalid_argument(writer.status().ToString());
                   }
                   return *std::move(writer);
                 }
               }),
           py::arg("filename"), py::arg("options") = BagzWriter::Options(),
           py::doc(kWriterInitDoc + 1))
      .def("__enter__", [](BagzWriter& self) -> BagzWriter& { return self; })
      .def(
          "__exit__",
          [](BagzWriter& self, py::handle exc_type, py::handle exc_value,
             py::handle traceback) {
            absl::Status status = self.Close();
            if (!status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          py::arg("exc_type"), py::arg("exc_value"), py::arg("traceback"),
          py::call_guard<py::gil_scoped_release>())
      .def(
          "write",
          [](BagzWriter* writer, absl::string_view record) {
            if (absl::Status status = writer->Write(record); !status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          py::arg("record"), py::call_guard<py::gil_scoped_release>(),
          py::doc(kWriterWriteDoc + 1))
      .def(
          "close",
          [](BagzWriter* writer) {
            if (absl::Status status = writer->Close(); !status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          py::call_guard<py::gil_scoped_release>(),
          py::doc(kWriterCloseDoc + 1))
      .def(
          "flush",
          [](BagzWriter* writer) {
            if (absl::Status status = writer->Flush(); !status.ok()) {
              throw std::invalid_argument(status.ToString());
            }
          },
          py::call_guard<py::gil_scoped_release>(),
          py::doc(kWriterFlushDoc + 1));
}

}  // namespace bagz
