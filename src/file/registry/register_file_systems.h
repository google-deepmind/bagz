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

#ifndef BAGZ_SRC_FILE_REGISTRY_REGISTER_FILE_SYSTEMS_H_
#define BAGZ_SRC_FILE_REGISTRY_REGISTER_FILE_SYSTEMS_H_

#include "src/file/registry/file_system_registry.h"

namespace bagz {

// Called once from `file_system/file.h` on first use to register all
// file-systems.
void RegisterFileSystems(FileSystemRegistry& registry);

}  // namespace bagz

#endif  // BAGZ_SRC_FILE_REGISTRY_REGISTER_FILE_SYSTEMS_H_
