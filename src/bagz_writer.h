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

// Reference implementation of Bagz writer for POSIX systems.

#ifndef BAGZ_SRC_BAGZ_WRITER_H_
#define BAGZ_SRC_BAGZ_WRITER_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/bagz_options.h"

namespace bagz {

// Writes a single Bagz shard. When open there are two FileWriters present, one
// called 'records' and the other called 'limits'. The 'records' file stores the
// (possibly compressed) records, and the 'limits' file stores the byte-offsets
// one-past-the-end of each record in 'records'.
//
// See README.md for more information.
class BagzWriter {
 public:
  // Move-only.
  BagzWriter(BagzWriter&&);
  ~BagzWriter();

  struct Options {
    // Where 'limits' data is placed on completion. While writing, 'limits' is
    // always written to a separate file.
    // If the limits_placement is `kTail` when the BagzWriter is closed, the
    // 'limits' are written to the end of 'records' and the `limits` file is
    // deleted, otherwise the 'limits' file is closed.
    LimitsPlacement limits_placement = LimitsPlacement::kTail;

    // Whether to compress the records with ZSTD before writing to 'records'.
    Compression compression = CompressionAutoDetect{};
  };

  // Opens two FileWriters, 'records' and 'limits', using file-system.
  // `filename` is the full path the 'records' FileWriter.
  // `filename` with added `limits.` prefix is the full path to the 'limits'
  // FileWriter.
  //
  // The `options` parameter contains the opening options as described above.
  //
  // If a file exists, it is truncated to zero length. If either file cannot be
  // opened/created, an error is returned.
  static absl::StatusOr<BagzWriter> OpenFile(absl::string_view filename,
                                             Options options);

  // Writes a single record into 'records'. Compresses according to the
  // `compression` option. Writes a little-endian `uint64_t` of the 'records'
  // size to 'limits'. Writes may be buffered but can be flushed with `Flush`.
  absl::Status Write(absl::string_view record);

  // Calls `Flush` on the 'records' and 'limits' FileWriters. When completed
  // the data written so far will be available to be read using `BagzReader`.
  //
  // Returns an error either if the 'records' or 'limits' FileWriters fail to
  // flush.
  absl::Status Flush();

  // Closes the BagzWriter
  //
  // When created with `options.limits_placement`
  //
  // * `kSeperate` - 'limits' and 'records' are closed.
  //
  // * `kTail` - the 'limits' are written to the end of 'records' and deleted.
  //   'records' is closed.
  //
  // Returns an error if any of the file operations fail. The data that was
  // successfully written will be recoverable using `BagzReader` regardless of
  // the `limits` placement.
  absl::Status Close();

 private:
  struct State;
  BagzWriter(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};

}  // namespace bagz

#endif  // BAGZ_SRC_BAGZ_WRITER_H_
