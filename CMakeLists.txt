# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.28)
project(bagz LANGUAGES CXX)

include(FetchContent)

option(BUILD_SHARED_LIBS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS TRUE)
set(CMAKE_SKIP_INSTALL_RULES ON)

option(ENABLE_CORE "Enable Core bagz extension" ON)
option(ENABLE_GCS "Enable GCS filesystem plugin" ON)

# Temporarily disable warnings for third-party libraries
set(SAVED_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -Wno-dev")

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module OPTIONAL_COMPONENTS Development.SABIModule)

include(cmake/third_party_absl.cmake)
include(cmake/third_party_nanobind.cmake)
include(cmake/third_party_zstd.cmake)

if(ENABLE_GCS)
  include(cmake/third_party_gcs.cmake)
endif()

# Restore original CXX_FLAGS
set(CMAKE_CXX_FLAGS "${SAVED_CMAKE_CXX_FLAGS}")

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS FALSE)

set(CMAKE_SKIP_INSTALL_RULES OFF)

include(cmake/bagz_cc.cmake)

set(bagz_internal_sources
  "src/internal/bagz_shard_reader.cc"
  "src/internal/limits_name.cc"
  "src/internal/parallel_do.cc"
  "src/internal/records_limits.cc"
  "src/internal/shard_indexing.cc"
  "src/internal/zstd_compressor.cc"
  "src/internal/zstd_decompressor.cc"
)
set(bagz_internal_headers
  "src/internal/bagz_shard_reader.h"
  "src/internal/limits_name.h"
  "src/internal/parallel_do.h"
  "src/internal/records_limits.h"
  "src/internal/recycling_pool.h"
  "src/internal/serialize.h"
  "src/internal/shard_indexing.h"
  "src/internal/zstd_compressor.h"
  "src/internal/zstd_decompressor.h"
)

bagz_cc_library(
  bagz_internal
  SOURCES ${bagz_internal_sources}
  HEADERS ${bagz_internal_headers}
  DEPS
    absl::strings
    absl::status
    absl::statusor
    absl::flat_hash_map
    absl::log
    absl::synchronization
    libzstd_static
)

set(bagz_file_system_sources
  "src/file/file_system/file_system.cc"
  "src/file/file_system/pread_file.cc"
  "src/file/file_system/shard_spec.cc"
  "src/file/file_system/string_pread_file.cc"
)
set(bagz_file_system_headers
  "src/file/file_system/file_system.h"
  "src/file/file_system/mock_file_system.h"
  "src/file/file_system/pread_file.h"
  "src/file/file_system/shard_spec.h"
  "src/file/file_system/string_pread_file.h"
  "src/file/file_system/write_file.h"
)

bagz_cc_library(
  bagz_file_system
  SOURCES ${bagz_file_system_sources}
  HEADERS ${bagz_file_system_headers}
  DEPS
    absl::flat_hash_map
    absl::log
    absl::status
    absl::statusor
    absl::strings
    bagz_internal
)

set(bagz_file_system_posix_sources
  "src/file/file_systems/posix/posix_file_system.cc"
)
set(bagz_file_system_posix_headers
  "src/file/file_systems/posix/posix_file_system.h"
)

bagz_cc_library(
  bagz_file_system_posix
  SOURCES ${bagz_file_system_posix_sources}
  HEADERS ${bagz_file_system_posix_headers}
  DEPS
    absl::status
    absl::statusor
    absl::strings
    bagz_file_system
    bagz_internal
)

if(ENABLE_GCS)
  set(bagz_file_system_gcs_sources
    "src/file/file_systems/gcs/gcs_file_system.cc"
  )
  set(bagz_file_system_gcs_headers
    "src/file/file_systems/gcs/gcs_file_system.h"
  )

  bagz_cc_library(
    bagz_file_system_gcs
    SOURCES ${bagz_file_system_gcs_sources}
    HEADERS ${bagz_file_system_gcs_headers}
    DEPS
      absl::log
      absl::status
      absl::statusor
      absl::strings
      bagz_file_system
      bagz_internal
      google-cloud-cpp::storage
  )
endif()

set(bagz_file_sources
  "src/file/file.cc"
  "src/file/registry/file_system_registry.cc"
  "src/file/registry/register_file_systems.cc"
)
set(bagz_file_headers
  "src/file/file.h"
  "src/file/registry/file_system_registry.h"
  "src/file/registry/register_file_systems.h"
)

bagz_cc_library(
  bagz_file
  SOURCES ${bagz_file_sources}
  HEADERS ${bagz_file_headers}
  DEPS
    absl::log
    absl::status
    absl::statusor
    absl::strings
    bagz_internal
    bagz_file_system
    bagz_file_system_posix
)

set(bagz_core_sources
  "src/bagz_index.cc"
  "src/bagz_multi_index.cc"
  "src/bagz_reader.cc"
  "src/bagz_writer.cc"
)
set(bagz_core_headers
  "src/bagz_index.h"
  "src/bagz_iterator.h"
  "src/bagz_multi_index.h"
  "src/bagz_options.h"
  "src/bagz_reader.h"
  "src/bagz_writer.h"
)

bagz_cc_library(
  bagz_core
  SOURCES ${bagz_core_sources}
  HEADERS ${bagz_core_headers}
  DEPS
    absl::flat_hash_map
    absl::log
    absl::status
    absl::statusor
    absl::strings
    bagz_file
    bagz_internal
    libzstd_static
)

set(bagz_python_sources
  "src/python/bagz.cc"
  "src/python/bagz_index.cc"
  "src/python/bagz_multi_index.cc"
  "src/python/bagz_options.cc"
  "src/python/bagz_reader.cc"
  "src/python/bagz_writer.cc"
)

option(ENABLE_CORE "Enable Core bagz extension" ON)
if(ENABLE_CORE)
  bagz_nanobind_extension(
    bagz
    SOURCES ${bagz_python_sources}

    DEPS
      absl::flat_hash_map
      absl::log
      absl::status
      absl::statusor
      absl::strings
      bagz_core
  )
endif()

if(ENABLE_GCS)
  bagz_nanobind_extension(
    bagz_gcs
    SOURCES "src/file/file_systems/gcs/bagz_gcs.cc"
    DEPS
      bagz_file_system_gcs
      bagz_file
      google-cloud-cpp::storage
      absl::log
      absl::status
  )
endif()

file(WRITE ${CMAKE_BINARY_DIR}/__init__.py [[# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from bagz.lib.bagz import *

import importlib.metadata
import logging

def _load_plugins():
  try:
    eps = importlib.metadata.entry_points(group='bagz.plugins')
  except TypeError:
    eps = importlib.metadata.entry_points().get('bagz.plugins', [])

  for ep in eps:
    try:
      ep.load()
    except Exception as e:
      logging.warning(f"Failed to load bagz plugin {ep.name}: {e}")

_load_plugins()
]])


if(ENABLE_CORE)
  install(
    FILES "${CMAKE_BINARY_DIR}/__init__.py"
    DESTINATION "bagz"
  )

  install(
    TARGETS bagz
    DESTINATION "bagz/lib"
  )
endif()

if(ENABLE_GCS)
  install(
    TARGETS bagz_gcs
    DESTINATION "bagz_gcs"
  )
endif()

install(
  DIRECTORY "beam/"
  DESTINATION bagz/beam
  FILES_MATCHING
  PATTERN "*.py"
)

install(
  FILES "src/python/bagz.pyi"
  DESTINATION "bagz"
  RENAME "__init__.pyi"
)

