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

#include <stdexcept>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "src/file/file_system/c_file_system.h"
#include "src/file/file_system/c_file_system_abi.h"
#include "src/file/registry/file_system_registry.h"
#include "src/python/bagz_index.h"
#include "src/python/bagz_multi_index.h"
#include "src/python/bagz_options.h"
#include "src/python/bagz_reader.h"
#include "src/python/bagz_writer.h"
#include "nanobind/nanobind.h"
#include "nanobind/stl/string_view.h"  // IWYU pragma: keep

namespace bagz {
namespace {

namespace nb = nanobind;

NB_MODULE(bagz, m) {
  m.doc() = "Bagz Python Bindings";
  RegisterBagzIndex(m);
  RegisterBagzMultiIndex(m);
  RegisterBagzOptions(m);
  RegisterBagzReader(m);
  RegisterBagzWriter(m);

  m.attr("FILESYSTEM_ABI_VERSION") = kFileSystemAbiVersion;

  m.def(
      "register_filesystem",
      [](std::string_view prefix, nb::object filesystem_capsule) {
        if (!nb::isinstance<nb::capsule>(filesystem_capsule)) {
          throw nb::type_error(
              "filesystem_capsule must be a PyCapsule object.");
        }
        nb::capsule cap = nb::borrow<nb::capsule>(filesystem_capsule);
        void* raw_c_fs = cap.data(kBagzCFileSystemCapsuleName);
        if (raw_c_fs == nullptr) {
          throw std::invalid_argument("filesystem_capsule pointer is null.");
        }
        absl::Status status =
            FileSystemRegistry::Instance().Register(prefix, raw_c_fs);
        if (!status.ok()) {
          if (absl::IsInvalidArgument(status)) {
            throw std::invalid_argument(std::string(status.message()));
          }
          throw std::runtime_error(std::string(status.message()));
        }
      },
      nb::arg("prefix"), nb::arg("filesystem_capsule"),
      "Registers a C-ABI filesystem plugin for the given prefix.");

  m.def(
      "unregister_filesystem",
      [](std::string_view prefix) {
        absl::Status status = FileSystemRegistry::Instance().Unregister(prefix);
        if (!status.ok()) {
          throw std::runtime_error(std::string(status.message()));
        }
      },
      nb::arg("prefix"),
      "Unregisters a filesystem previously registered for the given prefix.");

  // Shim to allow `from bagz import bagz` for backward compatibility.
  m.attr("bagz") = m;
}

}  // namespace
}  // namespace bagz
