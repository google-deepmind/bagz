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

#ifndef BAGZ_SRC_INTERNAL_RECYCLING_POOL_H_
#define BAGZ_SRC_INTERNAL_RECYCLING_POOL_H_

#include <memory>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace bagz::internal {

// Thread-safe object pool for creating and recycling objects.
//
// Objects can be requested from the pool and returned to the pool when they are
// no longer needed ("recycled" or "released"). Requests return recycled objects
// if available, and otherwise allocate new ones with "new" and initialized with
// the result of user-provided "factory" callable.
//
// Objects are returned as a unique_ptr with a special deleter that remembers
// the pool. Therefore, such unique_ptrs must either be destroyed or released
// before the pool is destroyed.
//
// Notes:
//
// 1. The object pool never shrinks, i.e. objects are never deallocated
//    (until the pool itself is destroyed).
// 2. All objects must be disassociated from the pool before the pool is
//    destroyed, by either destroying or releasing the special unique_ptrs.
//
// Example:
//
//   class RunCodeInPythonInterpreter {
//    public:
//
//     // Called from multiple threads.
//     int RunCode(absl::string_view code) const {
//       return pool_.Get([]{ return PythonInterpreter(); })->RunCode(code);
//     }
//
//    private:
//     RecyclingPool<PythonInterpreter> pool_;
//   }
//
template <typename T>
class RecyclingPool {
 public:
  // Recycler is a deleter for unique_ptr that returns the object to the pool.
  // Default constructed `Recycler`s must not be used for deletion.
  class Recycler {
   public:
    explicit Recycler(RecyclingPool* pool = nullptr) : pool_(pool) {}
    void operator()(T* ptr) { pool_->ReturnToPool(ptr); }

   private:
    RecyclingPool* pool_;
  };

  // Creates an object, or returns an existing object from the pool if possible.
  //
  // The factory is called to create a new object if no objects are available in
  // the pool. Thread-safe.
  template <typename Factory>
  std::unique_ptr<T, Recycler> Get(Factory&& factory)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    if (absl::MutexLock lock(&mutex_); !recycle_pool_.empty()) {
      T* object = recycle_pool_.back().release();
      recycle_pool_.pop_back();
      return std::unique_ptr<T, Recycler>(object, Recycler(this));
    }

    // No "else"; release the mutex first.
    return std::unique_ptr<T, Recycler>(
        new auto(std::forward<Factory>(factory)()), Recycler(this));
  }

 private:
  void ReturnToPool(T* object) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(&mutex_);
    recycle_pool_.emplace_back(object);
  }

  std::vector<std::unique_ptr<T>> recycle_pool_ ABSL_GUARDED_BY(mutex_);
  absl::Mutex mutex_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace bagz::internal

#endif  // BAGZ_SRC_INTERNAL_RECYCLING_POOL_H_
