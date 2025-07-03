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

#include "src/internal/parallel_do.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <semaphore>  // NOLINT(build/c++20)
#include <thread>     // NOLINT(build/c++11)
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

namespace bagz::internal {

absl::Status ParallelDo(size_t num_tasks,
                        absl::FunctionRef<absl::Status(size_t task_id)> func,
                        int max_parallelism, [[maybe_unused]] bool cpu_bound) {
  if (max_parallelism < 0) {
    return absl::InvalidArgumentError("max_parallelism must be non-negative");
  }
  max_parallelism = std::min<size_t>(num_tasks, max_parallelism);

  if (max_parallelism <= 1) {
    for (size_t task_id = 0; task_id < num_tasks; ++task_id) {
      absl::Status status = func(task_id);
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  // Prevent overflow in ParallelDoInternal.
  if (num_tasks > std::numeric_limits<size_t>::max() - max_parallelism) {
    num_tasks = std::numeric_limits<size_t>::max() - max_parallelism;
  }

  std::vector<absl::Status> thread_results(max_parallelism);
  std::vector<std::thread> threads;
  threads.reserve(max_parallelism);

  std::atomic_size_t failed_thread_id = num_tasks;
  std::atomic_size_t next_task = 0;
  for (int thread_id = 0; thread_id < max_parallelism; ++thread_id) {
    threads.emplace_back([&thread_results, &func, thread_id, num_tasks,
                          &next_task, &failed_thread_id]() {
      size_t task_id;
      while ((task_id = next_task.fetch_add(1, std::memory_order_relaxed)) <
             num_tasks) {
        thread_results[thread_id] = func(task_id);
        if (!thread_results[thread_id].ok()) {
          failed_thread_id.exchange(thread_id, std::memory_order_relaxed);
          next_task.exchange(num_tasks, std::memory_order_relaxed);
          return;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  size_t failed_thread_id_value = failed_thread_id.load();
  if (failed_thread_id_value < num_tasks) {
    return thread_results[failed_thread_id_value];
  }

  return absl::OkStatus();
}

class ParallelOperationImpl : public ParallelOperation {
 public:
  ParallelOperationImpl(
      absl::AnyInvocable<void(const std::atomic_bool& cancelled)> func)
      : func_(std::move(func)), cancelled_(false), done_{0}, thread_([this]() {
          func_(cancelled_);
          done_.release();
        }) {}

  virtual ~ParallelOperationImpl() {
    Cancel();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  bool Join(absl::Duration duration) override {
    if (duration == absl::InfiniteDuration()) {
      thread_.join();
      return true;
    } else {
      if (done_.try_acquire_for(absl::ToChronoMilliseconds(duration))) {
        thread_.join();
        return true;
      }
      return false;
    }
  }

  void Cancel() override { cancelled_.store(true, std::memory_order_relaxed); }

 private:
  absl::AnyInvocable<void(const std::atomic_bool& cancelled)> func_;
  std::atomic_bool cancelled_;
  std::binary_semaphore done_;
  std::thread thread_;
};

std::unique_ptr<ParallelOperation> ParallelOperation::Create(
    absl::AnyInvocable<void(const std::atomic_bool& cancelled)> func) {
  return std::make_unique<ParallelOperationImpl>(std::move(func));
}

}  // namespace bagz::internal
