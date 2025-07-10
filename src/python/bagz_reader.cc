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

#include "src/bagz_reader.h"

#include <Python.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/bagz_iterator.h"
#include "src/bagz_options.h"
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

class FileNotFoundError : public py::builtin_exception {
 public:
  using py::builtin_exception::builtin_exception;

  void set_error() const override {
    PyErr_SetString(PyExc_FileNotFoundError, what());
  }
};

// Helper class to allocate results for callback-based reads.
class IndexedAllocator {
 public:
  // Construct with GIL held.
  explicit IndexedAllocator(py::list& result) : result_(result) {}

  // Use callback without GIL held. GIL is released before returning.
  absl::Span<char> operator()(size_t result_index, ssize_t num_bytes) const {
    py::gil_scoped_acquire acquire;
    py::bytes record(nullptr, num_bytes);
    char* bytes;
    PyBytes_AsStringAndSize(record.ptr(), &bytes, &num_bytes);
    result_[result_index] = std::move(record);
    return absl::Span<char>(bytes, num_bytes);
  }

 private:
  py::list& result_;
};

// Helper to copy results from one list to another.
class IndexedCopy {
 public:
  // Construct with GIL held.
  explicit IndexedCopy(py::list& result) : result_(result) {}

  // Use callback with GIL held. GIL is released before returning.
  void operator()(size_t from_index, ssize_t to_index) const {
    py::gil_scoped_acquire acquire;
    result_[to_index] = result_[from_index];
  }

 private:
  py::list& result_;
};

void ThrowNonOkStatusAsException(const absl::Status& status) {
  if (!status.ok()) {
    if (absl::IsOutOfRange(status)) {
      throw py::index_error(std::string(status.message()));
    } else if (absl::IsNotFound(status)) {
      throw FileNotFoundError(status.ToString());
    }
    throw std::invalid_argument(status.ToString());
  }
}

constexpr char kOptionsDoc[] = R"(
Options for creating the bagz.Reader.

Args:
  sharding_layout: Specifies how input indexes/ranges are mapped to the
    underlying records within the shards. See README.md#sharding.
  limits_placement: Placement of the limits section on close defaulting to
    TAIL.
  compression: Compression algorithm to use defaulting to auto-detection.
  limits_storage: Whether to read the limits from disk for every read or to
    cache the limits in memory.
  max_parallelism: Maximum number of threads to use for operations that can be
    parallelized.
)";

constexpr char kInitDoc[] = R"(
Opens a collection of Bagz-formatted files (shards).

Args:
  file_spec: is either:
    * filename (e.g. "fs:/path/to/foo.bagz").
    * sharded file-spec (e.g. "fs:/path/to/foo@100.bagz").
    * comma-separated list of filenames and sharded file-specs
      (e.g. "fs:/path/to/f@3.bagz,fs:/path/to/bar.bagz").
  options: options to use when reading, see `bagz.Reader.Options`.
)";

BagzReader Init(py::object file_spec_obj, const BagzReader::Options& options) {
  static absl::NoDestructor<py::object> fspath(
      py::module::import("os").attr("fspath"));
  std::string file_spec = py::cast<std::string>((*fspath)(file_spec_obj));
  {
    py::gil_scoped_release release_gil;
    absl::StatusOr<BagzReader> reader = BagzReader::Open(file_spec, options);
    ThrowNonOkStatusAsException(reader.status());
    return *std::move(reader);
  }
}

constexpr char kReadRangeDoc[] = R"(
Returns all the records in the range [start, start + num_records).
)";

py::list ReadRange(const BagzReader& reader, size_t start, size_t num_records) {
  py::list result(num_records);
  {
    py::gil_scoped_release release;
    ThrowNonOkStatusAsException(reader.ReadRangeWithAllocator(
        start, num_records, IndexedAllocator(result)));
  }
  return result;
}

constexpr char kReadIndicesDoc[] = R"(
Returns the records at the given indices.
)";

py::list ReadIndicesFromSpan(const BagzReader& reader,
                             absl::Span<const size_t> indices) {
  py::list result(indices.size());
  {
    py::gil_scoped_release release;
    ThrowNonOkStatusAsException(reader.ReadIndicesWithAllocator(
        indices, IndexedAllocator(result), IndexedCopy(result)));
  }
  return result;
}

template <typename Int64>
py::list ReadIndicesFromNumpy(const BagzReader& reader,
                              py::array_t<Int64, py::array::c_style> indices) {
  static_assert(sizeof(Int64) == sizeof(size_t),
                "Int64 must be the same size as size_t");
  if (indices.ndim() != 1) {
    throw std::invalid_argument("indices must be a 1D array");
  }
  return ReadIndicesFromSpan(
      reader,
      absl::MakeConstSpan(reinterpret_cast<const size_t*>(indices.data()),
                          indices.shape()[0]));
}

py::list ReadIndicesFromIterable(const BagzReader& reader,
                                 std::vector<size_t> indices) {
  return ReadIndicesFromSpan(reader, indices);
}

py::list ReadIndicesFromSlice(const BagzReader& reader, py::slice slice) {
  ssize_t start, stop, step, slicelength;
  if (!slice.compute(static_cast<ssize_t>(reader.size()), &start, &stop, &step,
                     &slicelength)) {
    throw py::index_error("Invalid slice");
  }
  if (step == 1) {
    return ReadRange(reader, start, slicelength);
  }
  std::vector<size_t> indices_vector;
  indices_vector.reserve(slicelength);
  for (size_t i = start; i < stop; i += step) {
    indices_vector.push_back(i);
  }
  return ReadIndicesFromSpan(reader, indices_vector);
}

constexpr char kGetItemDoc[] = R"(
Returns the record at the given index.
)";

py::bytes GetItem(const BagzReader& reader, size_t index) {
  py::bytes result;
  {
    py::gil_scoped_release release;
    ThrowNonOkStatusAsException(reader.ReadWithAllocator(
        index, [&result](ssize_t num_bytes) -> absl::Span<char> {
          py::gil_scoped_acquire acquire;
          result = py::bytes(nullptr, num_bytes);
          char* bytes;
          PyBytes_AsStringAndSize(result.ptr(), &bytes, &num_bytes);
          return absl::Span<char>(bytes, num_bytes);
        }));
  }
  return result;
}

BagzReader GetSlice(const BagzReader& reader, py::slice slice) {
  ssize_t step, start, stop, slicelength;
  if (!slice.compute(static_cast<ssize_t>(reader.size()), &start, &stop, &step,
                     &slicelength)) {
    throw py::index_error("Invalid slice");
  }
  auto reader_slice = reader.Slice(start, step, slicelength);
  ThrowNonOkStatusAsException(reader_slice.status());
  return *std::move(reader_slice);
}

// Iterates over the reader and call a callback for each record in order.
// Early returns if the callback returns true.
// Every second, the GIL is acquired to check for signals.
// Returns whether any `callback` returned true.
template <typename CallBack>
bool AnyOf(BagzReader reader, CallBack&& callback) {
  py::gil_scoped_release release;
  BagzIterator iterator(std::move(reader));
  absl::Time time_start = absl::Now();
  for (;;) {
    auto result = iterator.next();
    if (!result.has_value()) {
      return false;
    }
    ThrowNonOkStatusAsException(result->status());
    if (callback(**result)) {
      return true;
    }
    absl::Time time_now = absl::Now();
    if (time_now - time_start > absl::Seconds(1)) {
      py::gil_scoped_acquire acquire;
      if (PyErr_CheckSignals() && PyErr_Occurred()) {
        throw py::error_already_set();
      }
      time_start = time_now;
    }
  }
}

constexpr char kIndexOfDoc[] = R"(
Returns the index of the first occurrence of the given value in the reader.

Raises a ValueError if the value is not found.
)";

size_t IndexOf(const BagzReader& reader, py::bytes value, size_t start,
               std::optional<size_t> stop) {
  absl::string_view bytes = py::cast<absl::string_view>(value);
  size_t index = start;
  auto reader_slice =
      reader.Slice(start, 1, stop.value_or(reader.size() - start));
  ThrowNonOkStatusAsException(reader_slice.status());
  if (!AnyOf(*std::move(reader_slice),
             [bytes, &index](absl::string_view record) {
               if (record == bytes) {
                 return true;
               }
               ++index;
               return false;
             })) {
    throw py::value_error("value is not in the bagz.Reader");
  }
  return index;
}

constexpr char kContainsDoc[] = R"(
Returns whether the given value is in the reader.
)";

bool Contains(const BagzReader& reader, py::bytes value) {
  auto bytes = py::cast<absl::string_view>(value);
  return AnyOf(reader,
               [bytes](absl::string_view record) { return record == bytes; });
}

constexpr char kCountDoc[] = R"(
Returns the number of occurrences of the given value in the reader.
)";

size_t Count(const BagzReader& reader, py::bytes value) {
  auto bytes = py::cast<absl::string_view>(value);
  size_t count = 0;
  AnyOf(reader, [bytes, &count](absl::string_view record) {
    if (record == bytes) {
      ++count;
    }
    return false;
  });
  return count;
}

// Iteration methods.

struct MakeBytes {
  py::bytes operator()(size_t num_bytes) const {
    py::gil_scoped_acquire acquire;
    return py::bytes(nullptr, num_bytes);
  }
};

struct SpanFromBytes {
  absl::Span<char> operator()(const py::bytes& result) const {
    char* bytes;
    ssize_t num_bytes;
    PyBytes_AsStringAndSize(result.ptr(), &bytes, &num_bytes);
    return absl::Span<char>(bytes, num_bytes);
  }
};

class ExceptionStore {
 public:
  void Store() {
    // Also clears the exception.
    PyErr_Fetch(&exception_, &value_, &traceback_);
  }
  void Restore() {
    if (HasException()) {
      PyErr_Restore(exception_, value_, traceback_);
      exception_ = nullptr;
      value_ = nullptr;
      traceback_ = nullptr;
    }
  }

  bool HasException() const { return exception_ != nullptr; }

  ~ExceptionStore() {
    Py_XDECREF(exception_);
    Py_XDECREF(value_);
    Py_XDECREF(traceback_);
  }

 private:
  PyObject* exception_ = nullptr;
  PyObject* value_ = nullptr;
  PyObject* traceback_ = nullptr;
};

// Helper to read batches of indices from a Python iterator.
class PythonBatchIterator {
 public:
  PythonBatchIterator(PyObject* indices_iter, ExceptionStore* exception_store)
      : indices_iter_(indices_iter), exception_store_(exception_store) {}

  // Takes up to read_ahead indices from indices_iter_ and returns them in
  // indices. Any exceptions are stored in exception_store.
  // Use callback with GIL not held. GIL is released before returning.
  // Returns whether there were no Python exceptions or StopIteration occurred.
  bool operator()(size_t, size_t read_ahead,
                  std::vector<size_t>& indices) const {
    if (exception_store_->HasException()) {
      return false;
    }
    indices.reserve(read_ahead);
    {
      py::gil_scoped_acquire acquire;
      for (size_t index = 0; index < read_ahead; ++index) {
        PyObject* iter_obj = PyIter_Next(indices_iter_);
        if (iter_obj == nullptr) {
          if (PyErr_Occurred() &&
              !PyErr_ExceptionMatches(PyExc_StopIteration)) {
            exception_store_->Store();
            return !indices.empty();
          }
          PyErr_Clear();
          return true;
        }
        absl::Cleanup cleanup = [iter_obj]() { Py_XDECREF(iter_obj); };

        PyObject* index_obj = PyNumber_Index(iter_obj);
        if (PyErr_Occurred()) {
          exception_store_->Store();
          return !indices.empty();
        }
        size_t result_index = PyLong_AsLongLong(index_obj);
        Py_DECREF(index_obj);
        if (result_index == size_t(-1)) {
          if (PyErr_Occurred()) {
            exception_store_->Store();
            return !indices.empty();
          }
        }
        indices.push_back(result_index);
      }
    }
    return true;
  }

  PyObject* indices_iter_;
  ExceptionStore* exception_store_;
};

class PythonIterator {
 public:
  // Iterator that returns py::bytes. Ensures GIL is held when creating/copying
  // py::bytes objects.
  using IteratorPyBytes =
      BagzIterator<MakeBytes, SpanFromBytes,
                   decltype([] { return py::gil_scoped_acquire(); })>;

  // Iterator that reads all records in the reader sequentially.
  PythonIterator(BagzReader reader, std::optional<size_t> read_ahead)
      : iterator_(
            std::make_unique<IteratorPyBytes>(std::move(reader), read_ahead)) {}

  // Iterator that reads records in the reader according to the sequence if
  // indices returned by index_iter.
  PythonIterator(BagzReader reader, py::object index_iter,
                 std::optional<size_t> read_ahead)
      : exception_store_(std::make_unique<ExceptionStore>()),
        index_iter_(std::move(index_iter)),
        iterator_(std::make_unique<IteratorPyBytes>(
            std::move(reader), read_ahead,
            PythonBatchIterator(index_iter_.ptr(), exception_store_.get()))) {}

  PythonIterator(PythonIterator&&) = default;
  ~PythonIterator() {
    if (iterator_ != nullptr) {
      py::gil_scoped_release release;
      iterator_ = nullptr;
    }
  }

  py::bytes next() {
    py::gil_scoped_release release;
    std::optional<absl::StatusOr<py::bytes>> result = iterator_->next();
    if (!result.has_value()) {
      if (exception_store_ != nullptr && exception_store_->HasException()) {
        py::gil_scoped_acquire acquire;
        exception_store_->Restore();
        throw py::error_already_set();
      }
      throw py::stop_iteration();
    }
    if (!result->ok()) {
      if (absl::IsAborted(result->status()) &&
          result->status().message().empty()) {
        if (exception_store_ != nullptr && exception_store_->HasException()) {
          py::gil_scoped_acquire acquire;
          exception_store_->Restore();
          if (PyErr_Occurred()) {
            throw py::error_already_set();
          }
        }
      }
      ThrowNonOkStatusAsException(result->status());
    }
    return *std::move(*result);
  }

 private:
  // Ensure exception_store_ address is valid if iterator is moved.
  // Can be Nullptr if no exception store is needed.
  std::unique_ptr<ExceptionStore> exception_store_;
  py::object index_iter_;
  std::unique_ptr<IteratorPyBytes> iterator_;
};

}  // namespace

void RegisterBagzReader(py::module& m) {
  auto register_sequence =
      py::module_::import("collections.abc").attr("Sequence").attr("register");

  auto reader = py::class_<BagzReader>(
      m, "Reader", "For reading a collection of Bagz-formatted shards.");

  auto reader_iterator = py::class_<PythonIterator>(
      m, "ReaderIterator", "Iterator for a BagzReader.");

  py::class_<BagzReader::Options>(reader, "Options", kOptionsDoc + 1)
      .def(
          py::init([](ShardingLayout sharding_layout,
                      LimitsPlacement limits_placement, Compression compression,
                      LimitsStorage limits_storage, int max_parallelism) {
            return BagzReader::Options{
                .sharding_layout = sharding_layout,
                .limits_placement = limits_placement,
                .compression = compression,
                .limits_storage = limits_storage,
                .max_parallelism = max_parallelism,
            };
          }),
          py::arg("sharding_layout") = BagzReader::Options{}.sharding_layout,
          py::arg("limits_placement") = BagzReader::Options{}.limits_placement,
          py::arg("compression") = BagzReader::Options{}.compression,
          py::arg("limits_storage") = BagzReader::Options{}.limits_storage,
          py::arg("max_parallelism") = BagzReader::Options{}.max_parallelism)
      .def_readwrite("sharding_layout", &BagzReader::Options::sharding_layout)
      .def_readwrite("limits_placement", &BagzReader::Options::limits_placement)
      .def_readwrite("compression", &BagzReader::Options::compression)
      .def_readwrite("limits_storage", &BagzReader::Options::limits_storage)
      .def_readwrite("max_parallelism", &BagzReader::Options::max_parallelism);

  reader
      .def(py::init(&Init), py::arg("file_spec"),
           py::arg("options") = BagzReader::Options{}, py::doc(kInitDoc + 1))
      .def("__len__", &BagzReader::size)
      .def("__getitem__", &GetItem, py::arg("index"), py::doc(kGetItemDoc + 1))
      .def("__getitem__", &GetSlice, py::arg("slice"), py::doc(kGetItemDoc + 1))
      .def("__reversed__",
           [](const BagzReader& reader) {
             if (reader.size() == 0) {
               return reader;
             } else {
               auto reverse_reader =
                   reader.Slice(reader.size() - 1, -1, reader.size());
               ThrowNonOkStatusAsException(reverse_reader.status());
               return *std::move(reverse_reader);
             }
           })
      .def("approximate_bytes_per_record",
           &BagzReader::ApproximateNumBytesPerRecord)
      .def("read",
           [](const BagzReader& reader) {
             return ReadRange(reader, 0, reader.size());
           })
      .def(
          "read_range_iter",
          [](const BagzReader& reader, std::size_t start,
             std::size_t num_records,
             std::optional<size_t> read_ahead = std::nullopt) {
            auto reader_slice = reader.Slice(start, 1, num_records);
            ThrowNonOkStatusAsException(reader_slice.status());
            return PythonIterator(*std::move(reader_slice), read_ahead);
          },
          py::arg("start"), py::arg("num_records"), py::kw_only(),
          py::arg("read_ahead") = std::nullopt)
      .def(
          "read_indices_iter",
          [](const BagzReader& reader, py::object indices_iterable,
             std::optional<size_t> read_ahead = std::nullopt) {
            PyObject* indices_iter = PyObject_GetIter(indices_iterable.ptr());
            if (PyErr_Occurred()) {
              throw py::error_already_set();
            }
            return PythonIterator(
                reader, py::reinterpret_steal<py::object>(indices_iter),
                read_ahead);
          },
          py::arg("indices"), py::kw_only(),
          py::arg("read_ahead") = std::nullopt)
      .def("__iter__",
           [](const BagzReader& reader) {
             return PythonIterator(reader, std::nullopt);
           })
      .def("__contains__", &Contains, py::arg("value"),
           py::doc(kContainsDoc + 1))
      .def("index", &IndexOf, py::arg("value"), py::arg("start") = 0,
           py::arg("stop") = std::nullopt, py::doc(kIndexOfDoc + 1))
      .def("count", &Count, py::arg("value"), py::doc(kCountDoc + 1))
      .def("read_range", &ReadRange, py::arg("start"), py::arg("num_records"),
           py::doc(kReadRangeDoc + 1))
      .def("read_indices", &ReadIndicesFromNumpy<int64_t>, py::arg("indices"),
           py::doc(kReadIndicesDoc + 1))
      .def("read_indices", &ReadIndicesFromNumpy<uint64_t>, py::arg("indices"),
           py::doc(kReadIndicesDoc + 1))
      .def("read_indices", &ReadIndicesFromSlice, py::arg("indices"),
           py::doc(kReadIndicesDoc + 1))
      .def("read_indices", &ReadIndicesFromIterable, py::arg("indices"),
           py::doc(kReadIndicesDoc + 1));

  reader_iterator.def("__next__", &PythonIterator::next);
  reader_iterator.def(
      "__iter__",
      [](PythonIterator& iterator) -> PythonIterator& { return iterator; });
  register_sequence(reader);
}

}  // namespace bagz
