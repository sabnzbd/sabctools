# Copyright 2007-2026 The SABnzbd-Team (sabnzbd.org)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

cmake_minimum_required(VERSION 3.15...4.0)
project(sabctools LANGUAGES CXX)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
include(ExternalProject)

# Use a compiler cache if one is on PATH. Worth having: the vendored tree below
# is 32 source files that do not depend on Python at all, so every extra Python
# version built in the same CI job recompiles exactly the same objects.
#
# Note this has no effect under the Visual Studio generators, which ignore
# CMAKE_<LANG>_COMPILER_LAUNCHER - only the Makefile and Ninja generators use it.
option(SABCTOOLS_COMPILER_CACHE "Use ccache/sccache when one is available" ON)
if(SABCTOOLS_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(COMPILER_CACHE NAMES sccache ccache)
    if(COMPILER_CACHE)
        message(STATUS "Using compiler cache: ${COMPILER_CACHE}")
        set(CMAKE_C_COMPILER_LAUNCHER "${COMPILER_CACHE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${COMPILER_CACHE}")
    endif()
endif()

# The version is declared in pyproject.toml and compiled in below, so there is
# only ever one to bump. scikit-build-core hands us the metadata version it
# resolved; a bare cmake invocation has to read the same file itself. Anchored at
# the line start so it cannot match cmake.version or target-version.
if(DEFINED SKBUILD_PROJECT_VERSION)
    set(SABCTOOLS_VERSION "${SKBUILD_PROJECT_VERSION}")
else()
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml" PYPROJECT)
    string(REGEX MATCH "[\r\n]version = \"([^\"]+)\"" _ "${PYPROJECT}")
    set(SABCTOOLS_VERSION "${CMAKE_MATCH_1}")
endif()
if(NOT SABCTOOLS_VERSION)
    message(FATAL_ERROR "could not determine the version from pyproject.toml")
endif()
message(STATUS "sabctools version: ${SABCTOOLS_VERSION}")

# ---------------------------------------------------------------------------
# Vendored dependencies
# ---------------------------------------------------------------------------
# rapidyenc is built by its own upstream CMake, as a nested project rather than
# add_subdirectory(): it sets global compile options and its own C++ standard.
# Keeping it at arm's length also keeps its per-ISA flag matrix and probes
# entirely upstream's problem, so re-vendoring picks up new kernels with no
# change here. The machinery below takes a project name for that reason - a
# second vendored tree should need no new code, only another call.
#
# ExternalProject inherits this project's generator, so the sub-build targets
# the same toolchain the extension does - and on Windows it inherits the
# environment that decides which toolchain that is. Note CMake's own compiler
# search runs "CC c++ g++ aCC cl ...", so under Ninja both c++ and g++ outrank
# cl: whatever GNU-ish compiler happens to be on PATH wins, be that MinGW gcc or
# the clang Visual Studio ships as c++.exe, and neither links against MSVC-built
# objects. CI therefore names cl.exe outright; see .github/actions/msvc-ninja.
# Build locally without any of that and CMake defaults to Visual Studio, which is
# equally fine here.

# A macOS universal2 build has to be sliced. Configured with several -arch flags
# at once, CMake reports a single CMAKE_SYSTEM_PROCESSOR - so the vendored CMake
# picks just one of IS_X86 / IS_ARM - and, worse, CHECK_CXX_COMPILER_FLAG probes
# with every -arch together, so -mavx2 and -march=armv8-a+crc both fail and are
# dropped for *all* slices. Such a build loses essentially all of its SIMD.
# Build one tree per architecture instead, and lipo the archives together.
set(SLICE_ARCHES "")
list(LENGTH CMAKE_OSX_ARCHITECTURES architecture_count)
if(APPLE AND architecture_count GREATER 1)
    set(SLICE_ARCHES ${CMAKE_OSX_ARCHITECTURES})
    message(STATUS "Slicing vendored libraries per architecture: ${SLICE_ARCHES}")
endif()

# Multi-config generators - Visual Studio, CMake's default on Windows unless
# something asks for Ninja - put their output in a per-config subdirectory.
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(CONFIG_SUBDIR "")
if(IS_MULTI_CONFIG)
    set(CONFIG_SUBDIR "Release/")
endif()

# Build one vendored CMake project and set <NAME>_ARCHIVES in the caller's scope:
# the static libraries produced, in the order given. Also leaves a target named
# <NAME>_built to depend on.
#
#   NAME        logical name, and the build directory under the binary dir
#   SOURCE_DIR  the vendored tree
#   SUBDIR      where in its build tree the archives land, "" for the top
#   LIBRARIES   archive base names, in link order: a static library only
#               satisfies symbols already demanded to its left
#   CMAKE_ARGS  extra configure arguments
function(add_vendored_project)
    cmake_parse_arguments(V "" "NAME;SOURCE_DIR;SUBDIR" "LIBRARIES;CMAKE_ARGS" ${ARGN})

    set(common_args
        -DCMAKE_BUILD_TYPE=Release
        # These static libraries end up inside a shared object
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        ${V_CMAKE_ARGS}
    )
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND common_args -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
    endif()

    # A nested project inherits nothing from this cache, so a compiler cache has
    # to be handed down explicitly - and this sub-build is the whole reason to
    # want one, being 32 source files against our six.
    foreach(launcher C_COMPILER_LAUNCHER CXX_COMPILER_LAUNCHER)
        if(CMAKE_${launcher})
            list(APPEND common_args -DCMAKE_${launcher}=${CMAKE_${launcher}})
        endif()
    endforeach()

    # One build per slice, or a single build targeting whatever we were told to
    set(slices ${SLICE_ARCHES})

    set(slice_targets "")
    set(slice_archives "")  # flat list, slice-major
    foreach(arch IN LISTS slices)
        _vendored_build(
            "${V_NAME}" "${V_SOURCE_DIR}" "${V_SUBDIR}" "${V_LIBRARIES}"
            "${common_args}" "${arch}" archives target
        )
        list(APPEND slice_targets ${target})
        list(APPEND slice_archives ${archives})
    endforeach()

    if(NOT slices)
        # Not slicing: one build, and CMAKE_OSX_ARCHITECTURES (if any) is
        # already inherited from the parent by ExternalProject.
        _vendored_build(
            "${V_NAME}" "${V_SOURCE_DIR}" "${V_SUBDIR}" "${V_LIBRARIES}"
            "${common_args}" "" archives target
        )
        set(${V_NAME}_ARCHIVES ${archives} PARENT_SCOPE)
        add_custom_target(${V_NAME}_built DEPENDS ${target})
        return()
    endif()

    # Merge slice by slice: lipo one fat archive per library
    set(merged_dir "${CMAKE_BINARY_DIR}/${V_NAME}-universal")
    file(MAKE_DIRECTORY "${merged_dir}")

    list(LENGTH V_LIBRARIES library_count)
    list(LENGTH slices slice_count)
    set(merged_archives "")
    math(EXPR last_library "${library_count} - 1")
    math(EXPR last_slice "${slice_count} - 1")
    foreach(index RANGE ${last_library})
        list(GET V_LIBRARIES ${index} library)
        set(merged "${merged_dir}/${CMAKE_STATIC_LIBRARY_PREFIX}${library}${CMAKE_STATIC_LIBRARY_SUFFIX}")

        # Pick this library's archive out of each slice. slice_archives holds
        # every slice's libraries end to end, in the order LIBRARIES gave them.
        set(inputs "")
        foreach(slice_index RANGE ${last_slice})
            math(EXPR flat "${slice_index} * ${library_count} + ${index}")
            list(GET slice_archives ${flat} input)
            list(APPEND inputs "${input}")
        endforeach()

        add_custom_command(
            OUTPUT "${merged}"
            COMMAND lipo -create ${inputs} -output "${merged}"
            DEPENDS ${slice_targets}
            COMMENT "Merging ${library} for ${slices}"
            VERBATIM
        )
        list(APPEND merged_archives "${merged}")
    endforeach()

    add_custom_target(${V_NAME}_built DEPENDS ${merged_archives})
    set(${V_NAME}_ARCHIVES ${merged_archives} PARENT_SCOPE)
endfunction()

# One ExternalProject. arch is "" for a plain build, or a single architecture.
# Sets out_archives and out_target in the caller's scope.
function(_vendored_build name source_dir subdir libraries cmake_args arch out_archives out_target)
    set(target "${name}")
    set(extra_args "")
    if(arch)
        set(target "${name}-${arch}")
        # CMAKE_SYSTEM_PROCESSOR alone is ignored: only setting the system name
        # as well puts CMake into cross-compiling mode, which is what makes it
        # honour the processor we hand it. nzbget's cmake/par2-turbo.cmake
        # passes the same pair for the same reason.
        set(extra_args
            -DCMAKE_OSX_ARCHITECTURES=${arch}
            -DCMAKE_SYSTEM_NAME=Darwin
            -DCMAKE_SYSTEM_PROCESSOR=${arch}
        )
    endif()

    set(binary_dir "${CMAKE_BINARY_DIR}/${target}")

    set(archives "")
    foreach(library IN LISTS libraries)
        list(APPEND archives
            "${binary_dir}/${subdir}${CONFIG_SUBDIR}${CMAKE_STATIC_LIBRARY_PREFIX}${library}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endforeach()

    ExternalProject_Add(${target}
        SOURCE_DIR "${source_dir}"
        BINARY_DIR "${binary_dir}"
        CMAKE_ARGS ${cmake_args} ${extra_args}
        BUILD_BYPRODUCTS ${archives}
        # Nothing to fetch, patch or install: the tree is vendored in-repo and
        # the archives are consumed straight out of the build directory.
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        INSTALL_COMMAND ""
        # Without this the build step is governed by a stamp file that knows
        # nothing about the vendored sources, so re-vendoring would silently
        # link the previous build's objects. Always hand off to the sub-build
        # and let its own generator decide what is out of date - that costs a
        # no-op ninja invocation when nothing changed.
        BUILD_ALWAYS ON
    )

    set(${out_archives} ${archives} PARENT_SCOPE)
    set(${out_target} ${target} PARENT_SCOPE)
endfunction()

# Only the static library is wanted; the shared one and the CLI tools would be
# built for nothing. Its archive lands in a subdirectory of its own, set by a
# target property upstream that an output-directory variable cannot override.
add_vendored_project(
    NAME rapidyenc
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/rapidyenc"
    SUBDIR "rapidyenc_static/"
    LIBRARIES rapidyenc
    CMAKE_ARGS -DDISABLE_SHARED=ON -DDISABLE_TOOL=ON
)

# ---------------------------------------------------------------------------
# Compiler flags for our own sources
# ---------------------------------------------------------------------------
# Nothing ISA-specific is decided here: every SIMD kernel lives in a vendored
# tree with its own architecture detection and per-file flag probes.
if(MSVC)
    # LTCG not enabled due to issues seen with code generation where different
    # ISA extensions are selected for specific files
    set(SABCTOOLS_FLAGS /O2 /GS- /Gy /sdl- /Oy /Oi)
    set(SABCTOOLS_LINK_FLAGS /OPT:REF /OPT:ICF)
else()
    # TODO: consider -flto - may require some extra testing
    set(SABCTOOLS_FLAGS
        -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
        -fomit-frame-pointer -fno-rtti -fno-exceptions -O3 -fwrapv
    )
    set(SABCTOOLS_LINK_FLAGS "")
endif()

# C++20 where the compiler has it, C++17 otherwise: STANDARD_REQUIRED OFF makes
# CMake decay to the newest standard the compiler actually supports.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Nothing here uses C++20 modules, so scanning every translation unit for them
# is wasted work - and with GCC it adds -fmodules-ts -fdeps-format=p1689r5,
# which ccache cannot parse ("to generate dependencies you must specify either
# -M or -MM"). CMake only turns scanning on for C++20 and above, which is why
# this bites our own sources but not the C++17 glue or the vendored trees.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ---------------------------------------------------------------------------
# The extension
# ---------------------------------------------------------------------------
python_add_library(sabctools MODULE WITH_SOABI
    src/sabctools.cc
    src/yenc.cc
    src/crc32.cc
    src/sparse.cc
    src/utils.cc
    src/unlocked_ssl.cc
)

add_dependencies(sabctools rapidyenc_built)

target_include_directories(sabctools PRIVATE src)
target_compile_options(sabctools PRIVATE ${SABCTOOLS_FLAGS})
target_compile_definitions(sabctools PRIVATE SABCTOOLS_VERSION="${SABCTOOLS_VERSION}")

if(NOT MSVC)
    target_compile_options(sabctools PRIVATE -Wno-missing-field-initializers)
endif()

# The vendored archives go last: a static library only satisfies symbols already
# demanded to its left, and it is our own objects that demand them.
target_link_libraries(sabctools PRIVATE
    ${rapidyenc_ARCHIVES}
    ${CMAKE_DL_LIBS}  # dlopen, for the unlocked SSL reads
)

if(SABCTOOLS_LINK_FLAGS)
    target_link_options(sabctools PRIVATE ${SABCTOOLS_LINK_FLAGS})
endif()

if(WIN32)
    target_link_libraries(sabctools PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
# Named explicitly rather than installing src/ as a package: that directory is
# mostly C++ sources and two vendored trees, none of which belong in the wheel.
install(TARGETS sabctools DESTINATION sabctools)
install(
    FILES
        src/__init__.py
        src/py.typed
        src/sabctools.pyi
    DESTINATION sabctools
)
