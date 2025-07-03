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

#ifndef BAGZ_SRC_INTERNAL_PARALLEL_DO_H_
#define BAGZ_SRC_INTERNAL_PARALLEL_DO_H_

#include <atomic>
#include <cstddef>
#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

namespace bagz::internal {

// Calls `func` with all task_ids in [0, `num_tasks`) concurrently.
// Stops on first task failure seen. This may not be the smallest task_id
// that would cause a failure. `max_parallelism` is the maximum number of
// tasks that can be running at the same time. `cpu_bound` is whether the
// function is CPU bound vs. IO bound. The implementation may use a
// different parallelism for CPU bound vs. IO bound bound functions.
// If max_parallelism is 0 or 1 or num_tasks is 0 or 1, `func` will be called
// serially.
absl::Status ParallelDo(size_t num_tasks,
                        absl::FunctionRef<absl::Status(size_t task_id)> func,
                        int max_parallelism, bool cpu_bound);

// A ParallelOperation is a task that can be cancelled.
// The task is started in a separate thread.
// The task is cancelled by calling Cancel().
// The task is joined by calling Join().
// The task is destroyed if Cancelled and Joined when this object is
// destroyed.
class ParallelOperation {
 public:
  static std::unique_ptr<ParallelOperation> Create(
      absl::AnyInvocable<void(const std::atomic_bool& cancelled)> func);
  virtual ~ParallelOperation() = default;
  // Blocking call to wait for task to complete.
  // Returns whether the task completed before the duration.
  virtual bool Join(absl::Duration duration) = 0;

  // Non-blocking call to cancel the task.
  // Sets the atomic_bool cancelled to true.
  virtual void Cancel() = 0;
};

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_PARALLEL_DO_H_
