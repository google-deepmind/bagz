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

#ifndef BAGZ_SRC_FILE_FILE_SYSTEM_WRITE_FILE_H_
#define BAGZ_SRC_FILE_FILE_SYSTEM_WRITE_FILE_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace bagz {

// An interface for writing to a file.
//
// Implementation note: Methods only need to support sequential calls but may
// be called from multiple threads. Writers must attempt to close the file on
// destruction ignoring any errors.
class WriteFile {
 public:
  virtual ~WriteFile() = default;

  // Writes the given `data` to the file. Returns an error if the write fails
  // to complete successfully.
  virtual absl::Status Write(absl::string_view data) = 0;

  // Flushes the file. Returns an error if the flush fails to complete
  // successfully.
  virtual absl::Status Flush() = 0;

  // Closes the file. Returns an error if the close fails to complete
  // successfully. May be called multiple times but only the first call will
  // have an effect. `Write` and `Flush` may not be called after `Close`.
  virtual absl::Status Close() = 0;
};

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_FILE_SYSTEM_WRITE_FILE_H_
