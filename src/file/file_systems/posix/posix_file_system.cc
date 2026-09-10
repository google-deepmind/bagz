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

#include "src/file/file_systems/posix/posix_file_system.h"

#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/file/file_system/file_system.h"
#include "src/file/file_system/pread_file.h"
#include "src/file/file_system/shard_spec.h"
#include "src/file/file_system/write_file.h"

namespace bagz {
namespace {

struct MmapArea {
  void* addr = nullptr;
  size_t size = 0;

  ~MmapArea() {
    if (addr != nullptr && addr != MAP_FAILED && size > 0) {
      munmap(addr, size);
    }
  }
};

using MmapSharedPtr = std::shared_ptr<MmapArea>;

absl::StatusOr<MmapSharedPtr> MmapFile(const std::string& filename) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "open");
  }
  struct stat stat;
  if (fstat(fd, &stat) < 0) {
    close(fd);
    return absl::ErrnoToStatus(errno, "fstat");
  }

  // We cannot mmap an empty file but we can use an empty string_view.
  if (stat.st_size == 0) {
    close(fd);
    return std::make_shared<MmapArea>();
  }

  void* records_mmap =
      mmap(/*addr=*/nullptr, /*length=*/stat.st_size, /*prot=*/PROT_READ,
           /*flags=*/MAP_SHARED, /*fd=*/fd, /*offset=*/0);
  close(fd);
  if (records_mmap == MAP_FAILED) {
    // kNotFound is confusing for user for ENODEV.
    if (errno == ENODEV) {
      return absl::PermissionDeniedError("mmap");
    } else {
      return absl::ErrnoToStatus(errno, "mmap");
    }
  }
  auto area = std::make_shared<MmapArea>();
  area->addr = records_mmap;
  area->size = stat.st_size;
  return area;
}

}  // namespace

class PosixFileSystem::EvictionQueue {
 public:
  EvictionQueue() = default;

  ~EvictionQueue() { Stop(); }

  void Stop() {
    {
      absl::MutexLock lock(mutex_);
      if (stop_) return;
      stop_ = true;
      cv_.Signal();
    }
    if (worker_started_.load(std::memory_order_acquire)) {
      pthread_join(worker_thread_, nullptr);
      worker_started_.store(false, std::memory_order_release);
    }
  }

  void Add(std::weak_ptr<MmapArea> mapping, void* addr, size_t size) {
    if (size == 0 || addr == nullptr) return;
    absl::call_once(start_once_, [this]() {
      if (pthread_create(&worker_thread_, nullptr, &EvictionQueue::ThreadMain,
                         this) == 0) {
        worker_started_.store(true, std::memory_order_release);
      }
    });
    absl::MutexLock lock(mutex_);
    if (stop_) return;
    if (pending_.size() < kMaxQueueSize) {
      bool was_empty = pending_.empty();
      pending_.push_back({std::move(mapping), addr, size});
      if (was_empty || pending_.size() >= kFlushThreshold) {
        cv_.Signal();
      }
    }
  }

 private:
  static void* ThreadMain(void* arg) {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "bagz_evict");
#endif
    static_cast<EvictionQueue*>(arg)->Run();
    return nullptr;
  }

  struct EvictionItem {
    std::weak_ptr<MmapArea> mapping;
    void* addr;
    size_t size;
  };

  static constexpr size_t kMaxQueueSize = 65536;
  static constexpr size_t kFlushThreshold = 4096;
  static constexpr absl::Duration kFlushInterval = absl::Seconds(1);

  void Run() {
    std::vector<EvictionItem> to_process;
    while (true) {
      {
        absl::MutexLock lock(mutex_);
        while (!stop_ && pending_.empty()) {
          cv_.Wait(&mutex_);
        }
        if (stop_ && pending_.empty()) {
          break;
        }
        absl::Time deadline = absl::Now() + kFlushInterval;
        while (!stop_ && pending_.size() < kFlushThreshold) {
          if (cv_.WaitWithDeadline(&mutex_, deadline)) {
            break;
          }
        }
        if (stop_ && pending_.empty()) {
          break;
        }
        to_process.swap(pending_);
      }
      if (!to_process.empty()) {
        ProcessRanges(to_process);
        to_process.clear();
      }
    }
  }

  static void ProcessRanges(std::vector<EvictionItem>& items) {
    const size_t page_size = sysconf(_SC_PAGESIZE);
    std::vector<std::shared_ptr<MmapArea>> active_mappings;
    std::vector<std::pair<uintptr_t, uintptr_t>> page_ranges;
    page_ranges.reserve(items.size());

    for (auto& item : items) {
      if (auto locked = item.mapping.lock()) {
        active_mappings.push_back(std::move(locked));
        uintptr_t start = reinterpret_cast<uintptr_t>(item.addr);
        uintptr_t end = start + item.size;
        uintptr_t page_start = start & ~(page_size - 1);
        uintptr_t page_end = (end + page_size - 1) & ~(page_size - 1);
        page_ranges.push_back({page_start, page_end});
      }
    }

    if (page_ranges.empty()) {
      return;
    }

    std::sort(page_ranges.begin(), page_ranges.end());

    std::vector<std::pair<uintptr_t, uintptr_t>> merged;
    merged.reserve(page_ranges.size());
    for (const auto& r : page_ranges) {
      if (merged.empty()) {
        merged.push_back(r);
      } else if (r.first <= merged.back().second) {
        merged.back().second = std::max(merged.back().second, r.second);
      } else {
        merged.push_back(r);
      }
    }

    for (const auto& [start, end] : merged) {
      madvise(reinterpret_cast<void*>(start), end - start, MADV_DONTNEED);
    }
  }

  absl::once_flag start_once_;
  absl::Mutex mutex_;
  absl::CondVar cv_;
  bool stop_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<EvictionItem> pending_ ABSL_GUARDED_BY(mutex_);
  pthread_t worker_thread_{};
  std::atomic<bool> worker_started_{false};
};

namespace {

class PosixPReadFile : public PReadFile {
 public:
  explicit PosixPReadFile(
      MmapSharedPtr mmap,
      PosixFileSystem::EvictionQueue* eviction_queue = nullptr)
      : mmap_(std::move(mmap)), eviction_queue_(eviction_queue) {}
  size_t size() const override { return mmap_ ? mmap_->size : 0; }

  absl::Status PRead(
      size_t offset, size_t num_bytes,
      absl::FunctionRef<bool(absl::string_view)> callback) const override {
    size_t mmap_size = size();
    if (num_bytes > mmap_size || offset > mmap_size - num_bytes) {
      return absl::OutOfRangeError("Invalid read");
    }
    if (num_bytes == 0) {
      callback(absl::string_view{});
      return absl::OkStatus();
    }
    const char* addr = static_cast<const char*>(mmap_->addr) + offset;
    callback(absl::string_view(addr, num_bytes));
    if (eviction_queue_ != nullptr) {
      eviction_queue_->Add(mmap_, const_cast<char*>(addr), num_bytes);
    }
    return absl::OkStatus();
  }

 private:
  MmapSharedPtr mmap_;
  PosixFileSystem::EvictionQueue* eviction_queue_ = nullptr;
};

class PosixWriteFile : public WriteFile {
 public:
  using File =
      std::unique_ptr<std::FILE,
                      decltype([](std::FILE* file) { std::fclose(file); })>;
  explicit PosixWriteFile(File file) : file_(std::move(file)) {}
  absl::Status Write(absl::string_view data) override {
    size_t written = std::fwrite(data.data(), 1, data.size(), file_.get());
    if (written != data.size()) {
      return absl::ErrnoToStatus(errno, "Failed to write to file");
    }
    return absl::OkStatus();
  }
  absl::Status Flush() override {
    if (std::fflush(file_.get()) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to flush file");
    }
    return absl::OkStatus();
  }
  absl::Status Close() override {
    if (file_ == nullptr) {
      return absl::OkStatus();
    }
    absl::Status status = absl::OkStatus();
    if (std::fclose(file_.release()) != 0) {
      status = absl::ErrnoToStatus(errno, "Failed to close file");
    }
    return status;
  }

 private:
  File file_;
};

}  // namespace

absl::StatusOr<absl_nonnull std::unique_ptr<WriteFile>>
PosixFileSystem::OpenWrite(absl::string_view filename, uint64_t offset,
                           absl::string_view options) const {
  std::string filename_str(filename);
  PosixWriteFile::File file;
  if (offset > 0) {
    file.reset(std::fopen(filename_str.c_str(), "ab"));
    if (file == nullptr) {
      return absl::ErrnoToStatus(errno, "Failed to open file");
    }
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to seek to end of file");
    }
    if (size_t file_size = std::ftell(file.get()); file_size < offset) {
      return absl::OutOfRangeError(
          absl::StrCat("Invalid offset: ", offset, " > ", file_size));
    }
    if (ftruncate(fileno(file.get()), offset) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to truncate file");
    }
    if (std::fseek(file.get(), offset, SEEK_SET) != 0) {
      return absl::ErrnoToStatus(errno, "Failed to seek file");
    }
  } else {
    file.reset(std::fopen(filename_str.c_str(), "wb"));
    if (file == nullptr) {
      return absl::ErrnoToStatus(errno, "Failed to open file");
    }
  }
  return std::make_unique<PosixWriteFile>(std::move(file));
}

PosixFileSystem::PosixFileSystem() = default;

PosixFileSystem::~PosixFileSystem() = default;

PosixFileSystem::EvictionQueue* PosixFileSystem::GetEvictionQueue() const {
  absl::call_once(eviction_queue_init_once_, [this]() {
    eviction_queue_ = std::make_unique<EvictionQueue>();
  });
  return eviction_queue_.get();
}

absl::StatusOr<absl_nonnull std::unique_ptr<PReadFile>>
PosixFileSystem::OpenPRead(absl::string_view filename,
                           absl::string_view options) const {
  std::string filename_str(filename);
  absl::StatusOr<MmapSharedPtr> mmap = MmapFile(filename_str.c_str());
  if (!mmap.ok()) {
    return mmap.status();
  }
  EvictionQueue* queue =
      absl::StrContains(options, "no_evict") ? nullptr : GetEvictionQueue();
  return std::make_unique<PosixPReadFile>(*std::move(mmap), queue);
}

absl::Status PosixFileSystem::Delete(absl::string_view filename,
                                     absl::string_view options) const {
  std::string filename_str(filename);
  if (std::remove(filename_str.c_str()) != 0) {
    return absl::ErrnoToStatus(errno, "Failed to delete file");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<absl_nonnull std::unique_ptr<PReadFile>>>
PosixFileSystem::BulkOpenPRead(absl::string_view filespec_without_prefix,
                               absl::string_view options) const {
  std::string filespec = CanonicaliseShardSpec(
      filespec_without_prefix, [](const std::string& pattern) {
        glob_t glob_result;
        absl::Cleanup cleanup = [&glob_result] { globfree(&glob_result); };
        int return_value =
            glob(pattern.c_str(), GLOB_NOESCAPE, nullptr, &glob_result);
        if (return_value != 0 || glob_result.gl_pathc == 0) {
          return std::string{};
        }
        return std::string(glob_result.gl_pathv[glob_result.gl_pathc - 1]);
      });

  return FileSystem::BulkOpenPRead(filespec, options);
}

}  // namespace bagz
