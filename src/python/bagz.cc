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

#include "absl/strings/str_format.h"
#include "src/file/registry/file_system_registry.h"
#include "src/python/bagz_index.h"
#include "src/python/bagz_multi_index.h"
#include "src/python/bagz_options.h"
#include "src/python/bagz_reader.h"
#include "src/python/bagz_writer.h"
#include "nanobind/nanobind.h"

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
      "_get_registry_capsule",
      [](int plugin_abi_version) {
        if (plugin_abi_version != kFileSystemAbiVersion) {
          throw std::runtime_error(absl::StrFormat(
              "Incompatible bagz filesystem ABI: plugin was built with ABI %d, "
              "but bagz core provides ABI %d. Please update or rebuild the "
              "plugin.",
              plugin_abi_version, kFileSystemAbiVersion));
        }
        return nb::capsule(&FileSystemRegistry::Instance(),
                           "FileSystemRegistry");
      },
      nb::arg("plugin_abi_version") = 1);

  // Shim to allow `from bagz import bagz` for backward compatibility.
  m.attr("bagz") = m;
}

}  // namespace
}  // namespace bagz
