# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
#
# synthis-et4.18: sfizz 1.2.3 (BSD-2-Clause) as a static library for the WCLAP module (wasm32, Emscripten, no
# threads). Only reached with SYNTHIS_WEBCLAP_PROCESSOR=sampler. Its own directory so that the directory-wide
# compile options below reach sfizz's targets and no others.
#
# The submodule is not built in place: it is copied (external/ linked, except the patched atomic_queue) into the build tree at
# configure time and ../patches/sfizz-1.2.3-no-threads.patch is applied to the copy, so third_party/sfizz is never
# modified. Options follow cmake/Sfizz.cmake (the native sampler build); the differences are marked WASM.
# See docs/PLUGIN_AS_WCLAP_SAMPLER_SPIKE_2026-11-04.md.

set(SYNTHIS_SFIZZ_SOURCE "${PLUGIN_ROOT}/third_party/sfizz" CACHE PATH "sfizz 1.2.3 checkout (the submodule)")
set(_patch "${CMAKE_CURRENT_SOURCE_DIR}/../patches/sfizz-1.2.3-no-threads.patch")
set(_copy "${CMAKE_BINARY_DIR}/sfizz-src")
if(NOT EXISTS "${SYNTHIS_SFIZZ_SOURCE}/src/sfizz/FilePool.cpp")
    message(FATAL_ERROR "sfizz sources not found in ${SYNTHIS_SFIZZ_SOURCE} (git submodule update --init --recursive)")
endif()

# Copy and patch once per patch content; the stamp holds the patch's hash.
file(SHA256 "${_patch}" _patch_hash)
set(_stamp "${_copy}/.synthis-patched")
set(_have "")
if(EXISTS "${_stamp}")
    file(READ "${_stamp}" _have)
endif()
if(NOT _have STREQUAL _patch_hash)
    file(REMOVE_RECURSE "${_copy}")
    file(MAKE_DIRECTORY "${_copy}")
    file(GLOB _top LIST_DIRECTORIES true "${SYNTHIS_SFIZZ_SOURCE}/*")
    foreach(_entry IN LISTS _top)
        get_filename_component(_name "${_entry}" NAME)
        if(_name STREQUAL "external")
            # Linked, not copied (abseil and simde are ~60 MB), except atomic_queue, which the patch changes.
            file(MAKE_DIRECTORY "${_copy}/external")
            file(GLOB _ext LIST_DIRECTORIES true "${_entry}/*")
            foreach(_e IN LISTS _ext)
                get_filename_component(_en "${_e}" NAME)
                if(_en STREQUAL "atomic_queue")
                    file(COPY "${_e}" DESTINATION "${_copy}/external")
                else()
                    file(CREATE_LINK "${_e}" "${_copy}/external/${_en}" SYMBOLIC)
                endif()
            endforeach()
        elseif(NOT _name MATCHES "^(benchmarks|tests|demos|devtools)$")
            file(COPY "${_entry}" DESTINATION "${_copy}")
        endif()
    endforeach()
    execute_process(COMMAND patch -p1 --forward --input "${_patch}" WORKING_DIRECTORY "${_copy}"
                    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "sfizz patch did not apply: ${_out}")
    endif()
    file(WRITE "${_stamp}" "${_patch_hash}")
    message(STATUS "sfizz copied to ${_copy} and patched (no threads)")
endif()

set(SFIZZ_SHARED        OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER        OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO          OFF CACHE BOOL "" FORCE)
set(SFIZZ_JACK          OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS         OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS         OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS      OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS    OFF CACHE BOOL "" FORCE)
set(SFIZZ_USE_SNDFILE   OFF CACHE BOOL "" FORCE)   # dr_libs, never the LGPL libsndfile
set(SFIZZ_GIT_SUBMODULE_CHECK OFF CACHE BOOL "" FORCE)
set(USE_LIBCPP          OFF CACHE BOOL "" FORCE)   # WASM: Emscripten's libc++ is the only one; no -stdlib flags
# WASM: Emscripten's toolchain says CMAKE_SYSTEM_PROCESSOR x86, which would add -msse2 and the AVX sources' -mavx.
# "wasm32" matches none of sfizz's x86 or arm branches, so it builds the portable (scalar) SIMD helpers.
set(PROJECT_SYSTEM_PROCESSOR "wasm32" CACHE STRING "" FORCE)

# WASM: the kit's flags (-msimd128 -fwasm-exceptions, and -sWASM_LEGACY_EXCEPTIONS=0 for exnref) on every sfizz
# object: one exceptions form per module. SFIZZ_NO_THREADS switches the patch on.
add_compile_options(${JUCE_WEBCLAP_FLAGS} -O3 -Wno-unused-function -Wno-unused-parameter -Wno-deprecated-declarations)
add_compile_definitions(SFIZZ_NO_THREADS=1)

set(CMAKE_CXX_STANDARD 14)   # as cmake/Sfizz.cmake: sfizz and its abseil at their own standard
add_subdirectory("${_copy}" "${CMAKE_BINARY_DIR}/sfizz-build" EXCLUDE_FROM_ALL)

# clang 21 (emsdk-4) makes this an error in the vendored atomic_queue; see cmake/Sfizz.cmake.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-error=missing-template-arg-list-after-template-kw" SYNTHIS_HAVE_WNOERROR_TEMPLATE_KW)
if(SYNTHIS_HAVE_WNOERROR_TEMPLATE_KW)
    foreach(_t IN ITEMS sfizz_internal sfizz_static sfizz_parser sfizz_messaging sfizz_spin_mutex sfizz_import)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE "-Wno-error=missing-template-arg-list-after-template-kw")
        endif()
    endforeach()
endif()

if(NOT TARGET sfizz::sfizz)
    message(FATAL_ERROR "sfizz configured but sfizz::sfizz does not exist")
endif()
