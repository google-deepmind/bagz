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

# Macro for creating static or header-only libraries
# Usage:
# bagz_cc_library(library_name
#   [SOURCES src1.cc src2.cc ...]
#   [HEADERS hdr1.h hdr2.h ...]
#   [DEPS dep1 dep2 ...]
#   [ALWAYS_LINK_DEPS dep1 dep2 ...]
# )
macro(bagz_cc_library NAME)
    cmake_parse_arguments(
        PARSED_ARGS
        "" # OPTIONS
        "" # ONE_VALUE_KEYWORDS
        "SOURCES;HEADERS;DEPS;ALWAYS_LINK_DEPS" # MULTI_VALUE_KEYWORDS
        ${ARGN}
    )

    if(PARSED_ARGS_SOURCES)
        # Create a static library
        add_library(${NAME} STATIC ${PARSED_ARGS_SOURCES} ${PARSED_ARGS_HEADERS})
        target_include_directories(${NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        if(PARSED_ARGS_DEPS)
            target_link_libraries(${NAME} PRIVATE ${PARSED_ARGS_DEPS})
        endif()
    else()
        # Create a header-only (INTERFACE) library
        if(NOT PARSED_ARGS_HEADERS)
            message(WARNING "bagz_cc_library created header-only library ${NAME} with no HEADERS specified.")
        endif()
        add_library(${NAME} INTERFACE)
        target_include_directories(${NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
        # Add headers to the interface library target for IDE support (optional but good practice)
        # Ensure HEADERS were provided before trying to use them
        if(PARSED_ARGS_HEADERS)
             target_sources(${NAME} INTERFACE ${PARSED_ARGS_HEADERS})
        endif()
        if(PARSED_ARGS_DEPS)
            target_link_libraries(${NAME} INTERFACE ${PARSED_ARGS_DEPS})
        endif()
        if(PARSED_ARGS_ALWAYS_LINK_DEPS)
            target_link_libraries(${NAME}
                INTERFACE -Wl,--whole-archive
                ${PARSED_ARGS_ALWAYS_LINK_DEPS}
                -Wl,--no-whole-archive)
        endif()
    endif()
endmacro()


# Macro for creating pybind11 Python modules
# Usage:
# bagz_pybind11_extension(module_name
#   SOURCES src1.cc src2.cc ...
#   [DEPS dep1 dep2 ...]
#   [ALWAYS_LINK_DEPS dep1 dep2 ...]
# )
macro(bagz_pybind11_extension NAME)
    cmake_parse_arguments(
        PARSED_ARGS
        "" # OPTIONS
        "" # ONE_VALUE_KEYWORDS
        "SOURCES;DEPS;ALWAYS_LINK_DEPS" # MULTI_VALUE_KEYWORDS
        ${ARGN}
    )

    if(NOT PARSED_ARGS_SOURCES)
        message(FATAL_ERROR "bagz_pybind11_extension ${NAME} requires SOURCES to be specified.")
    endif()

    pybind11_add_module(${NAME} SHARED ${PARSED_ARGS_SOURCES})

    # Add the directory where the macro is called to the include path.
    # This allows sources to include headers relative to this directory,
    # for example: #include "src/python/some_header.h" if the macro is called
    # from the project root and the header is in src/python/.
    target_include_directories(${NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

    if(PARSED_ARGS_DEPS)
        target_link_libraries(${NAME} PRIVATE ${PARSED_ARGS_DEPS})
    endif()

    if(PARSED_ARGS_ALWAYS_LINK_DEPS)
        target_link_libraries(${NAME}
            PRIVATE -Wl,--whole-archive
            ${PARSED_ARGS_ALWAYS_LINK_DEPS}
            -Wl,--no-whole-archive)
    endif()

endmacro()
