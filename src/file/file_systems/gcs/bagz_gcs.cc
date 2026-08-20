// Copyright 2026 Google LLC
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

#include "absl/base/no_destructor.h"
#include "absl/log/absl_check.h"
#include "src/file/file_systems/gcs/gcs_file_system.h"
#include "src/file/registry/file_system_registry.h"
#include "nanobind/nanobind.h"

namespace bagz {
namespace {

NB_MODULE(bagz_gcs, m) {
  namespace nb = nanobind;
  nb::module_ bagz_lib = nb::module_::import_("bagz.lib.bagz");
  nb::capsule cap =
      nb::cast<nb::capsule>(bagz_lib.attr("_get_registry_capsule")());
  FileSystemRegistry* registry = static_cast<FileSystemRegistry*>(cap.data());

  static absl::NoDestructor<GcsFileSystem> gcs_fs;
  ABSL_CHECK_OK(registry->Register("gs:", *gcs_fs));
}

}  // namespace
}  // namespace bagz
