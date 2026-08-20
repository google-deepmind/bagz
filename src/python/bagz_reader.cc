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
#include "nanobind/nanobind.h"
#include "nanobind/ndarray.h"
#include "nanobind/stl/optional.h"  // IWYU pragma: keep
#include "nanobind/stl/string.h"  // IWYU pragma: keep
#include "nanobind/stl/string_view.h"  // IWYU pragma: keep
#include "nanobind/stl/variant.h"  // IWYU pragma: keep
#include "nanobind/stl/vector.h"  // IWYU pragma: keep

namespace bagz {
namespace {

namespace nb = nanobind;

// Helper class to allocate results for callback-based reads.
class IndexedAllocator {
 public:
  // Construct with GIL held.
  explicit IndexedAllocator(nb::list& result) : result_(result) {}

  // Use callback without GIL held. GIL is released before returning.
  absl::Span<char> operator()(size_t result_index, Py_ssize_t num_bytes) const {
    nb::gil_scoped_acquire acquire;
    nb::bytes record(nullptr, num_bytes);
    char* bytes;
    PyBytes_AsStringAndSize(record.ptr(), &bytes, &num_bytes);
    result_[result_index] = std::move(record);
    return absl::Span<char>(bytes, num_bytes);
  }

 private:
  nb::list& result_;
};

// Helper class to copy results for callback-based reads.
class IndexedCopy {
 public:
  // Construct with GIL held.
  explicit IndexedCopy(nb::list& result) : result_(result) {}

  // Use callback without GIL held. GIL is released before returning.
  void operator()(size_t from_index, Py_ssize_t to_index) const {
    nb::gil_scoped_acquire acquire;
    result_[to_index] = result_[from_index];
  }

 private:
  nb::list& result_;
};

void ThrowNonOkStatusAsException(const absl::Status& status) {
  if (!status.ok()) {
    nb::gil_scoped_acquire acquire;
    if (absl::IsOutOfRange(status)) {
      throw nb::index_error(std::string(status.message()).c_str());
    } else if (absl::IsNotFound(status)) {
      PyErr_SetString(PyExc_FileNotFoundError, status.ToString().c_str());
      nb::raise_python_error();
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

BagzReader Init(nb::object file_spec_obj, const BagzReader::Options& options) {
  static absl::NoDestructor<nb::object> fspath(
      nb::module_::import_("os").attr("fspath"));
  std::string file_spec = nb::cast<std::string>((*fspath)(file_spec_obj));
  {
    nb::gil_scoped_release release_gil;
    absl::StatusOr<BagzReader> reader = BagzReader::Open(file_spec, options);
    ThrowNonOkStatusAsException(reader.status());
    return *std::move(reader);
  }
}

constexpr char kReadRangeDoc[] = R"(
Returns all the records in the range [start, start + num_records).
)";

nb::list ReadRange(const BagzReader& reader, size_t start, size_t num_records) {
  nb::list result;
  for (size_t i = 0; i < num_records; ++i) {
    result.append(nb::none());
  }
  {
    nb::gil_scoped_release release;
    ThrowNonOkStatusAsException(reader.ReadRangeWithAllocator(
        start, num_records, IndexedAllocator(result)));
  }
  return result;
}

constexpr char kReadIndicesDoc[] = R"(
Returns the records at the given indices.
)";

nb::list ReadIndicesFromSpan(const BagzReader& reader,
                             absl::Span<const size_t> indices) {
  nb::list result;
  for (size_t i = 0; i < indices.size(); ++i) {
    result.append(nb::none());
  }
  {
    nb::gil_scoped_release release;
    ThrowNonOkStatusAsException(reader.ReadIndicesWithAllocator(
        indices, IndexedAllocator(result), IndexedCopy(result)));
  }
  return result;
}

template <typename Int64>
nb::list ReadIndicesFromNumpy(const BagzReader& reader,
                              nb::ndarray<Int64, nb::c_contig> indices) {
  static_assert(sizeof(Int64) == sizeof(size_t),
                "Int64 must be the same size as size_t");
  if (indices.ndim() != 1) {
    throw std::invalid_argument("indices must be a 1D array");
  }
  return ReadIndicesFromSpan(
      reader,
      absl::MakeConstSpan(reinterpret_cast<const size_t*>(indices.data()),
                          indices.shape(0)));
}

nb::list ReadIndicesFromIterable(const BagzReader& reader,
                                 std::vector<size_t> indices) {
  return ReadIndicesFromSpan(reader, indices);
}

nb::list ReadIndicesFromSlice(const BagzReader& reader, nb::slice slice) {
  auto comp = slice.compute(reader.size());
  Py_ssize_t start = comp.get<0>();
  Py_ssize_t step = comp.get<2>();
  size_t slicelength = comp.get<3>();

  if (step == 1) {
    return ReadRange(reader, start, slicelength);
  }
  std::vector<size_t> indices_vector;
  indices_vector.reserve(slicelength);
  for (size_t i = 0; i < slicelength; ++i) {
    indices_vector.push_back(start + i * step);
  }
  return ReadIndicesFromSpan(reader, indices_vector);
}

constexpr char kGetItemDoc[] = R"(
Returns the record at the given index.
)";

nb::bytes GetItem(const BagzReader& reader, size_t index) {
  nb::bytes result;
  {
    nb::gil_scoped_release release;
    ThrowNonOkStatusAsException(reader.ReadWithAllocator(
        index, [&result](Py_ssize_t num_bytes) -> absl::Span<char> {
          nb::gil_scoped_acquire acquire;
          result = nb::bytes(nullptr, num_bytes);
          char* bytes;
          PyBytes_AsStringAndSize(result.ptr(), &bytes, &num_bytes);
          return absl::Span<char>(bytes, num_bytes);
        }));
  }
  return result;
}

BagzReader GetSlice(const BagzReader& reader, nb::slice slice) {
  auto comp = slice.compute(reader.size());
  Py_ssize_t start = comp.get<0>();
  Py_ssize_t step = comp.get<2>();
  size_t slicelength = comp.get<3>();

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
  nb::gil_scoped_release release;
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
      nb::gil_scoped_acquire acquire;
      if (PyErr_CheckSignals() && PyErr_Occurred()) {
        nb::raise_python_error();
      }
      time_start = time_now;
    }
  }
}

constexpr char kIndexOfDoc[] = R"(
Returns the index of the first occurrence of the given value in the reader.

Raises a ValueError if the value is not found.
)";

size_t IndexOf(const BagzReader& reader, nb::bytes value, size_t start,
               std::optional<size_t> stop) {
  absl::string_view bytes((const char*)value.data(), value.size());
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
    throw nb::value_error("value is not in the bagz.Reader");
  }
  return index;
}

constexpr char kContainsDoc[] = R"(
Returns whether the given value is in the reader.
)";

bool Contains(const BagzReader& reader, nb::bytes value) {
  absl::string_view bytes((const char*)value.data(), value.size());
  return AnyOf(reader,
               [bytes](absl::string_view record) { return record == bytes; });
}

constexpr char kCountDoc[] = R"(
Returns the number of occurrences of the given value in the reader.
)";

size_t Count(const BagzReader& reader, nb::bytes value) {
  absl::string_view bytes((const char*)value.data(), value.size());
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
  nb::bytes operator()(size_t num_bytes) const {
    nb::gil_scoped_acquire acquire;
    return nb::bytes(nullptr, num_bytes);
  }
};

struct SpanFromBytes {
  absl::Span<char> operator()(const nb::bytes& result) const {
    char* bytes;
    Py_ssize_t num_bytes;
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

// // Helper to read batches of indices from a Python iterator.
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
      nb::gil_scoped_acquire acquire;
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
  // Iterator that returns nb::bytes. Ensures GIL is held when creating/copying
  // nb::bytes objects.
  using IteratorPyBytes =
      BagzIterator<MakeBytes, SpanFromBytes,
                   decltype([] { return nb::gil_scoped_acquire(); })>;

  // Iterator that reads all records in the reader sequentially.
  PythonIterator(BagzReader reader, std::optional<size_t> read_ahead)
      : iterator_(
            std::make_unique<IteratorPyBytes>(std::move(reader), read_ahead)) {}

  // Iterator that reads records in the reader according to the sequence if
  // indices returned by index_iter.
  PythonIterator(BagzReader reader, nb::object index_iter,
                 std::optional<size_t> read_ahead)
      : exception_store_(std::make_unique<ExceptionStore>()),
        index_iter_(std::move(index_iter)),
        iterator_(std::make_unique<IteratorPyBytes>(
            std::move(reader), read_ahead,
            PythonBatchIterator(index_iter_.ptr(), exception_store_.get()))) {}

  PythonIterator(PythonIterator&&) = default;
  ~PythonIterator() {
    if (iterator_ != nullptr) {
      nb::gil_scoped_release release;
      iterator_ = nullptr;
    }
  }

  nb::bytes next() {
    nb::gil_scoped_release release;
    std::optional<absl::StatusOr<nb::bytes>> result = iterator_->next();
    if (!result.has_value()) {
      if (exception_store_ != nullptr && exception_store_->HasException()) {
        nb::gil_scoped_acquire acquire;
        exception_store_->Restore();
        nb::raise_python_error();
      }
      throw nb::stop_iteration();
    }
    if (!result->ok()) {
      if (absl::IsAborted(result->status()) &&
          result->status().message().empty()) {
        if (exception_store_ != nullptr && exception_store_->HasException()) {
          nb::gil_scoped_acquire acquire;
          exception_store_->Restore();
          if (PyErr_Occurred()) {
            nb::raise_python_error();
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
  nb::object index_iter_;
  std::unique_ptr<IteratorPyBytes> iterator_;
};

}  // namespace

void RegisterBagzReader(nb::module_& m) {
  auto register_sequence =
      nb::module_::import_("collections.abc").attr("Sequence").attr("register");

  auto reader = nb::class_<BagzReader>(
      m, "Reader", "For reading a collection of Bagz-formatted shards.");

  auto reader_iterator = nb::class_<PythonIterator>(
      m, "ReaderIterator", "Iterator for a BagzReader.");

  nb::class_<BagzReader::Options>(reader, "Options", kOptionsDoc + 1)
      .def(
          "__init__",
          [](BagzReader::Options* self, ShardingLayout sharding_layout,
             LimitsPlacement limits_placement, Compression compression,
             LimitsStorage limits_storage, int max_parallelism) {
            new (self) BagzReader::Options{
                .sharding_layout = sharding_layout,
                .limits_placement = limits_placement,
                .compression = compression,
                .limits_storage = limits_storage,
                .max_parallelism = max_parallelism,
            };
          },
          nb::arg("sharding_layout") = BagzReader::Options{}.sharding_layout,
          nb::arg("limits_placement") = BagzReader::Options{}.limits_placement,
          nb::arg("compression") = BagzReader::Options{}.compression,
          nb::arg("limits_storage") = BagzReader::Options{}.limits_storage,
          nb::arg("max_parallelism") = BagzReader::Options{}.max_parallelism)
      .def_rw("sharding_layout", &BagzReader::Options::sharding_layout)
      .def_rw("limits_placement", &BagzReader::Options::limits_placement)
      .def_rw("compression", &BagzReader::Options::compression)
      .def_rw("limits_storage", &BagzReader::Options::limits_storage)
      .def_rw("max_parallelism", &BagzReader::Options::max_parallelism)
      .def("__getstate__",
           [](const BagzReader::Options& options) {
             return nb::make_tuple(
                 options.sharding_layout, options.limits_placement,
                 options.compression, options.limits_storage,
                 options.max_parallelism, options.read_ahead_bytes);
           })
      .def("__setstate__", [](BagzReader::Options* self, nb::tuple t) {
        if (t.size() != 6) {
          throw nb::type_error("Invalid state for BagzReader.Options!");
        }
        new (self) BagzReader::Options{
            .sharding_layout = nb::cast<ShardingLayout>(t[0]),
            .limits_placement = nb::cast<LimitsPlacement>(t[1]),
            .compression = nb::cast<Compression>(t[2]),
            .limits_storage = nb::cast<LimitsStorage>(t[3]),
            .max_parallelism = nb::cast<int>(t[4]),
            .read_ahead_bytes = nb::cast<std::optional<size_t>>(t[5]),
        };
      });

  reader
      .def(
          "__init__",
          [](BagzReader* self, nb::object file_spec_obj,
             const BagzReader::Options& options) {
            new (self) BagzReader(Init(file_spec_obj, options));
          },
          nb::arg("file_spec"), nb::arg("options") = BagzReader::Options{},
          kInitDoc + 1)
      .def_prop_ro("options", &BagzReader::options)
      .def_prop_ro("file_spec", &BagzReader::filespec)
      .def("__getstate__",
           [](const BagzReader& reader) {
             if (reader.filespec().empty()) {
               throw nb::type_error(
                   "Cannot pickle BagzReader opened without a file_spec path.");
             }
             return nb::make_tuple(reader.filespec(), reader.options(),
                                   reader.slice_start(), reader.slice_step(),
                                   reader.slice_length());
           })
      .def("__setstate__",
           [](BagzReader* self, nb::tuple t) {
             if (t.size() != 5) {
               throw nb::type_error("Invalid state for BagzReader!");
             }
             std::string filespec = nb::cast<std::string>(t[0]);
             BagzReader::Options options = nb::cast<BagzReader::Options>(t[1]);
             size_t slice_start = nb::cast<size_t>(t[2]);
             int64_t slice_step = nb::cast<int64_t>(t[3]);
             size_t slice_length = nb::cast<size_t>(t[4]);

             absl::StatusOr<BagzReader> reader =
                 BagzReader::Open(filespec, std::move(options));
             ThrowNonOkStatusAsException(reader.status());

             if (slice_start == 0 && slice_step == 1 &&
                 slice_length == reader->size()) {
               new (self) BagzReader(std::move(*reader));
               return;
             }
             absl::StatusOr<BagzReader> slice =
                 reader->Slice(slice_start, slice_step, slice_length);
             ThrowNonOkStatusAsException(slice.status());
             new (self) BagzReader(std::move(*slice));
           })
      .def("__len__", &BagzReader::size)
      .def("__getitem__", &GetItem, nb::arg("index"), kGetItemDoc + 1)
      .def("__getitem__", &GetSlice, nb::arg("slice"), kGetItemDoc + 1)
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
          nb::arg("start"), nb::arg("num_records"), nb::kw_only(),
          nb::arg("read_ahead") = std::nullopt)
      .def(
          "read_indices_iter",
          [](const BagzReader& reader, nb::object indices_iterable,
             std::optional<size_t> read_ahead = std::nullopt) {
            PyObject* indices_iter = PyObject_GetIter(indices_iterable.ptr());
            if (PyErr_Occurred()) {
              nb::raise_python_error();
            }
            return PythonIterator(reader, nb::steal<nb::object>(indices_iter),
                                  read_ahead);
          },
          nb::arg("indices"), nb::kw_only(),
          nb::arg("read_ahead") = std::nullopt)
      .def("__iter__",
           [](const BagzReader& reader) {
             return PythonIterator(reader, std::nullopt);
           })
      .def("__contains__", &Contains, nb::arg("value"), kContainsDoc + 1)
      .def("index", &IndexOf, nb::arg("value"), nb::arg("start") = 0,
           nb::arg("stop") = std::nullopt, kIndexOfDoc + 1)
      .def("count", &Count, nb::arg("value"), kCountDoc + 1)
      .def("read_range", &ReadRange, nb::arg("start"), nb::arg("num_records"),
           kReadRangeDoc + 1)
      .def("read_indices", &ReadIndicesFromNumpy<int64_t>, nb::arg("indices"),
           kReadIndicesDoc + 1)
      .def("read_indices", &ReadIndicesFromNumpy<uint64_t>, nb::arg("indices"),
           kReadIndicesDoc + 1)
      .def("read_indices", &ReadIndicesFromSlice, nb::arg("indices"),
           kReadIndicesDoc + 1)
      .def("read_indices", &ReadIndicesFromIterable, nb::arg("indices"),
           kReadIndicesDoc + 1);

  reader_iterator.def("__next__", &PythonIterator::next);
  reader_iterator.def("__iter__", [](nb::handle self) { return self; });
  register_sequence(reader);
}

}  // namespace bagz
