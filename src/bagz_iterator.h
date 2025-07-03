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

#ifndef BAGZ_SRC_BAGZ_ITERATOR_H_
#define BAGZ_SRC_BAGZ_ITERATOR_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "src/bagz_reader.h"
#include "src/internal/parallel_do.h"

namespace bagz {

using SequenceReadBatch = absl::AnyInvocable<bool(
    size_t offset, size_t num_records, std::vector<size_t>&) const>;

// Iterator for reading records from a BagzReader.
//
// The iterator reads records in batches of size `read_ahead` if specified,
// otherwise an estimate of the number of records based on read_ahead_bytes
// setting in `bagz::Reader::Options`. The records are read in parallel.
//
// The iterator buffers the records in a vector in reverse order so they can be
// moved out of the buffer efficiently.
//
// The iterator is cancelled when the destructor is called.
// BufferEditGuard is created whenever the internal buffer is cleared or
// elements are copied but not when elements are moved or created.

template <typename ResultMaker, typename SpanFromResult,
          typename BufferEditGuard>
class BagzIterator {
 public:
  using Result = std::invoke_result_t<ResultMaker, size_t>;

  // If collection is not empty, it will be called with the offset and the
  // number of records to read and the indices of the records to read.
  BagzIterator(BagzReader reader,
               std::optional<size_t> read_ahead = std::nullopt,
               SequenceReadBatch&& read_batch = {},
               ResultMaker&& make_result = {},
               SpanFromResult&& span_from_result = {},
               BufferEditGuard&& buffer_edit_guard = {})
      : reader_(std::move(reader)),
        make_result_(std::forward<ResultMaker>(make_result)),
        span_from_result_(std::forward<SpanFromResult>(span_from_result)),
        buffer_edit_guard_(std::forward<BufferEditGuard>(buffer_edit_guard)) {
    size_t num_ahead;
    if (read_ahead.has_value()) {
      num_ahead = *read_ahead;
    } else {
      double bytes_per_record = reader_.ApproximateNumBytesPerRecord();
      if (bytes_per_record < 8) {
        // Use the size an index in the limits file instead.
        bytes_per_record = 8;
      }
      num_ahead = 1ull + (reader_.options().read_ahead_bytes.value_or(
                              BagzReader::Options::kDefaultReadAheadBytes) /
                          bytes_per_record);
    }
    num_ahead = std::min(num_ahead, reader_.size());
    if (num_ahead == 0) {
      if (read_batch) {
        num_ahead = 1;
      } else {
        more_to_read_ = false;
        return;
      }
    }
    parallel_operation_ = internal::ParallelOperation::Create(
        [this, num_ahead, read_batch = std::move(read_batch)](
            const std::atomic_bool& cancelled) {
          std::vector<std::optional<Result>> buffer;
          buffer.reserve(num_ahead);
          absl::Cleanup cleanup([this, &buffer, &read_batch]() {
            auto guard = buffer_edit_guard_();
            buffer.clear();
          });
          std::vector<size_t> indices;
          for (size_t offset = 0;; offset += num_ahead) {
            if (cancelled.load(std::memory_order_relaxed)) {
              return;
            }
            absl::Status status;
            if (read_batch) {
              indices.clear();
              if (!read_batch(offset, num_ahead, indices)) {
                status = absl::AbortedError("");
              } else if (!indices.empty()) {
                {
                  absl::MutexLock lock(&buffer_mutex_);
                  buffer.resize(indices.size(), std::nullopt);
                }
                status = reader_.ReadIndicesWithAllocator(
                    indices,
                    std::bind_front(&BagzIterator::AllocateForIndex, this,
                                    &buffer),
                    std::bind_front(&BagzIterator::CopyResult, this, &buffer));
              }
            } else {
              size_t end_position =
                  std::min(reader_.size(), offset + num_ahead);
              if (end_position > offset) {
                size_t batch_size = end_position - offset;
                {
                  absl::MutexLock lock(&buffer_mutex_);
                  buffer.resize(batch_size, std::nullopt);
                }
                status = reader_.ReadRangeWithAllocator(
                    offset, batch_size,
                    std::bind_front(&BagzIterator::AllocateForIndex, this,
                                    &buffer));
              }
            }
            bool last_batch = buffer.size() < num_ahead;
            {
              absl::MutexLock lock(&buffer_mutex_);
              while (!buffer_.empty()) {
                buffer_available_.Wait(&buffer_mutex_);
              }
              if (status.ok()) {
                std::swap(buffer_, buffer);
              } else {
                status_ = status;
              }
              if (last_batch) {
                more_to_read_ = false;
              }
              next_available_.Signal();
              if (last_batch || !status_.ok()) {
                break;
              }
            }
          }
        });
  }

  ~BagzIterator() {
    if (parallel_operation_ != nullptr) {
      parallel_operation_->Cancel();
      // Clear the result buffer to allow double buffer to be used.
      ClearBuffer();
      // Wake up the the double buffer.
      buffer_available_.SignalAll();
      // Wait for the parallel operation to finish.
      parallel_operation_->Join(absl::InfiniteDuration());
      // Clear the result buffer again as more records may have been added
      // during the parallel operation.
      ClearBuffer();
    }
  }

  // Returns the next record or nullopt if there are no more records.
  // Returns an error if there was an error reading the records.
  std::optional<absl::StatusOr<Result>> next() {
    std::optional<absl::StatusOr<Result>> result = std::nullopt;
    bool buffer_empty = false;
    {
      absl::MutexLock lock(&buffer_mutex_);
      while (buffer_.empty() && status_.ok() && more_to_read_) {
        next_available_.Wait(&buffer_mutex_);
      }
      if (status_.ok()) {
        if (buffer_.empty()) {
          return std::nullopt;
        }
        result = std::move(*buffer_.back());
        buffer_.pop_back();
      } else {
        result = status_;
      }
      buffer_empty = buffer_.empty();
    }
    if (buffer_empty) {
      buffer_available_.Signal();
    }
    return result;
  }

 private:
  void ClearBuffer() {
    absl::MutexLock lock(&buffer_mutex_);
    auto guard = buffer_edit_guard_();
    buffer_.clear();
  }

  absl::Span<char> AllocateForIndex(std::vector<std::optional<Result>>* buffer,
                                    size_t result_index, size_t record_size) {
    auto& result = (*buffer)[buffer->size() - 1 - result_index];
    result.emplace(make_result_(record_size));
    return span_from_result_(*result);
  }

  void CopyResult(std::vector<std::optional<Result>>* buffer, size_t from_index,
                  size_t to_index) {
    size_t rev_from_index = buffer->size() - 1 - from_index;
    size_t rev_to_index = buffer->size() - 1 - to_index;
    auto guard = buffer_edit_guard_();
    (*buffer)[rev_to_index] = (*buffer)[rev_from_index];
  }

  BagzReader reader_;
  size_t more_to_read_ = true;
  absl::Mutex buffer_mutex_;
  absl::CondVar next_available_;
  absl::CondVar buffer_available_;
  std::vector<std::optional<Result>> buffer_ ABSL_GUARDED_BY(buffer_mutex_);
  absl::Status status_ ABSL_GUARDED_BY(buffer_mutex_);
  std::unique_ptr<internal::ParallelOperation> parallel_operation_;
  [[no_unique_address]] ResultMaker make_result_;
  [[no_unique_address]] SpanFromResult span_from_result_;
  [[no_unique_address]] BufferEditGuard buffer_edit_guard_;
};

namespace internal {

struct StringMaker {
  std::string operator()(size_t num_bytes) const {
    return std::string(num_bytes, '\0');
  }
};

struct SpanFromString {
  absl::Span<char> operator()(std::string& result) const {
    return absl::Span<char>(result.data(), result.size());
  }
};

struct NoOpEditGuard {
  int operator()() const { return 0; }
};

}  // namespace internal

template <typename ResultMaker = internal::StringMaker,
          typename SpanFromResult = internal::SpanFromString,
          typename BufferEditGuard = internal::NoOpEditGuard>
BagzIterator(BagzReader reader, std::optional<size_t> read_ahead = std::nullopt,
             SequenceReadBatch&& read_batch = {},
             ResultMaker&& make_result = {},
             SpanFromResult&& span_from_result = {},
             BufferEditGuard&& buffer_edit_guard = {})
    -> BagzIterator<ResultMaker, SpanFromResult, BufferEditGuard>;

}  // namespace bagz

#endif  // BAGZ_SRC_BAGZ_ITERATOR_H_
