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

#include <Python.h>

#include "src/python/bagz_index.h"
#include "src/python/bagz_multi_index.h"
#include "src/python/bagz_options.h"
#include "src/python/bagz_reader.h"
#include "src/python/bagz_writer.h"
#include "pybind11/pybind11.h"

namespace bagz {
namespace {

PYBIND11_MODULE(bagz, m) {
  m.doc() = "Bagz Python Bindings";
  RegisterBagzIndex(m);
  RegisterBagzMultiIndex(m);
  RegisterBagzOptions(m);
  RegisterBagzReader(m);
  RegisterBagzWriter(m);

  // Shim to allow `from bagz import bagz` for backward compatibility.
  m.attr("bagz") = m;
}

}  // namespace
}  // namespace bagz
