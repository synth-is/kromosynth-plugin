# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# sfizz vendoring — Task 2 only, gated behind KROMOSYNTH_BUILD_SAMPLER.
#
# KEEPABLE.
#
# Rewritten 2026-08-27 against the actual submodule at tag 1.2.3. The previous
# version was written before the submodule existed and was wrong in three ways
# that only reading the vendored CMake could reveal:
#
#   • It forced SFIZZ_LV2, SFIZZ_LV2_UI, SFIZZ_VST and SFIZZ_AU. None of those
#     options exist in 1.2.3 — the plugin wrappers moved to the (now archived)
#     sfizz-ui repository, and the old names were PLUGIN_* anyway. Four dead
#     cache entries that read as if they were doing something.
#   • It did not force ENABLE_LTO, which is a real option and defaults ON.
#   • Its libsndfile guard could not fail. See the licence section below.
#
# Constraints this encodes, from WP_SYNTHIS_PLUGIN.md §3 and the session brief:
#
#   • sfizz is BSD-2-Clause and embedded as a LIBRARY, not a process.
#   • Do NOT vendor libsndfile. sfizz's default audio reader is dr_libs
#     (public domain / MIT-0). Confirmed stronger than "toggled off": the
#     submodule set is abseil-cpp, gulrak/filesystem, simde, dr_libs, libaiff
#     and WavPack. libsndfile is not in the tree at all.
#   • No extra dependencies beyond JUCE and sfizz.
#
# Maintenance risk, restated so it is not lost between documents: sfizz's last
# tagged library release is 1.2.3 (January 2024) and sfizz-ui was archived by
# its owner in June 2026. Pin to the tag; revisit at Stage 3; liquidsfz is the
# fallback. (WP §3, §9.)

set(KROMOSYNTH_SFIZZ_DIR "${CMAKE_SOURCE_DIR}/third_party/sfizz")
set(KROMOSYNTH_SFIZZ_TAG "1.2.3")

if(NOT EXISTS "${KROMOSYNTH_SFIZZ_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "KROMOSYNTH_BUILD_SAMPLER=ON but third_party/sfizz is empty.\n"
        "Add it as a pinned submodule first:\n"
        "    git submodule add https://github.com/sfztools/sfizz third_party/sfizz\n"
        "    git -C third_party/sfizz checkout ${KROMOSYNTH_SFIZZ_TAG}\n"
        "    git add third_party/sfizz\n"
        "    git submodule update --init --recursive")
endif()

# ── The pin actually being the pin ───────────────────────────────────────────
#
# `git submodule add` records the gitlink at the default-branch tip. A manual
# `git -C third_party/sfizz checkout 1.2.3` afterwards moves only the working
# tree, and the next `git submodule update` silently reverts it. That is not
# hypothetical: it happened on the first attempt here, leaving the tree at
# 1.2.3-39-gf5c6e29f while `project(libsfizz VERSION 1.2.3)` still said 1.2.3,
# so the version string gave no warning. `git add third_party/sfizz` after the
# checkout is the missing step. This check is what would have caught it.

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --always
        WORKING_DIRECTORY "${KROMOSYNTH_SFIZZ_DIR}"
        OUTPUT_VARIABLE KROMOSYNTH_SFIZZ_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    if(NOT KROMOSYNTH_SFIZZ_DESCRIBE STREQUAL KROMOSYNTH_SFIZZ_TAG)
        message(FATAL_ERROR
            "third_party/sfizz is at '${KROMOSYNTH_SFIZZ_DESCRIBE}', expected "
            "'${KROMOSYNTH_SFIZZ_TAG}'.\n"
            "Re-pin with:\n"
            "    git -C third_party/sfizz checkout ${KROMOSYNTH_SFIZZ_TAG}\n"
            "    git add third_party/sfizz\n"
            "    git submodule update --init --recursive\n"
            "If the pin is being deliberately moved, update "
            "KROMOSYNTH_SFIZZ_TAG in this file and re-verify the licence "
            "assertions below against the new tag.")
    endif()
    message(STATUS "sfizz pinned at ${KROMOSYNTH_SFIZZ_DESCRIBE}")
else()
    message(WARNING "git not found — cannot verify the sfizz pin")
endif()

# ── Licence: detect a renamed option ─────────────────────────────────────────
#
# The previous guard read SFIZZ_USE_SNDFILE back after add_subdirectory and
# errored if it was ON. That check could never fire. We FORCE the cache entry
# ourselves, sfizz's option() leaves an existing cache entry untouched, so the
# read-back returns our own write. In the exact scenario the comment named — a
# renamed option — SFIZZ_USE_SNDFILE would remain an orphan entry at OFF, the
# guard would pass, and sfizz would take its new option's default.
#
# Two checks that can fail instead: the option name must still be present in
# sfizz's own CMakeLists (catches a rename), and the target must not carry the
# compile definition the ON path sets (catches everything else).

file(READ "${KROMOSYNTH_SFIZZ_DIR}/CMakeLists.txt" KROMOSYNTH_SFIZZ_ROOT_CMAKE)
if(NOT KROMOSYNTH_SFIZZ_ROOT_CMAKE MATCHES "SFIZZ_USE_SNDFILE")
    message(FATAL_ERROR
        "SFIZZ_USE_SNDFILE is not declared in third_party/sfizz/CMakeLists.txt. "
        "The option has been renamed or removed, so forcing it below would be "
        "a no-op and sfizz may fall back to the LGPL libsndfile reader. Find "
        "the new option name before proceeding.")
endif()

# ── Apple silicon: sfizz 1.2.3 does not build on arm64 ──────────────────────
#
# SfizzConfig.cmake at this tag reads:
#
#     elseif(PROJECT_SYSTEM_PROCESSOR MATCHES "(arm.*)")
#         add_compile_options(-mfpu=neon)
#         if(NOT ANDROID)
#             add_compile_options(-mfloat-abi=hard)
#
# "arm64" matches "arm.*", so 32-bit ARM flags land on an arm64 target and
# clang rejects both, breaking abseil and wavpack before sfizz itself is
# reached. Upstream later added `AND NOT (PROJECT_SYSTEM_PROCESSOR MATCHES
# "(arm64)")`, but that fix is on the development branch and is in NO tag. The
# last tagged library release does not build on the only platform Stage 0
# targets. That is evidence for WP §9's sfizz-maintenance question, not a
# detail.
#
# PROJECT_SYSTEM_PROCESSOR is read in exactly three places at this tag: these
# flags, a status line, and SfizzSIMDSourceFiles.cmake — which branches on x86
# names only, so an arm64 host takes the same path under either spelling.
# Pre-setting the equivalent "aarch64" spelling therefore changes nothing
# except which broken branch is skipped. sfizz guards its own assignment with
# `if(NOT PROJECT_SYSTEM_PROCESSOR)`, so setting it first wins.
#
# This is a workaround for an upstream defect, deliberately narrow and
# deliberately loud. Revisit when the pin moves.

if(KROMOSYNTH_UNIVERSAL_BINARY)
    message(FATAL_ERROR
        "KROMOSYNTH_UNIVERSAL_BINARY with sfizz ${KROMOSYNTH_SFIZZ_TAG} is not "
        "supported. sfizz derives PROJECT_SYSTEM_PROCESSOR from "
        "CMAKE_OSX_ARCHITECTURES, which for a universal build is the list "
        "'arm64;x86_64'. That matches the broken arm branch, and no single "
        "value produces correct flags for both slices. Settle the pin question "
        "in WP_SYNTHIS_PLUGIN.md §9 before building universal with the sampler.")
endif()

set(PROJECT_SYSTEM_PROCESSOR "aarch64" CACHE STRING
    "Overridden: sfizz 1.2.3 applies 32-bit ARM flags whenever this matches arm.*"
    FORCE)

# ── Build only the static library ────────────────────────────────────────────
# Every other sfizz target is a liability here: the tools pull in extra deps
# and the demos are irrelevant. Defaults at 1.2.3 are noted where they differ
# from what we want, since forcing something already off is just documentation.

set(SFIZZ_SHARED        OFF CACHE BOOL "" FORCE)  # default ON
set(SFIZZ_RENDER        OFF CACHE BOOL "" FORCE)  # default ON — pulls cxxopts
set(ENABLE_LTO          OFF CACHE BOOL "" FORCE)  # default ON
set(SFIZZ_JACK          OFF CACHE BOOL "" FORCE)  # Linux-gated, off here anyway
set(SFIZZ_TESTS         OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS         OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS      OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS    OFF CACHE BOOL "" FORCE)

# The licence-critical one. dr_libs instead of libsndfile.
set(SFIZZ_USE_SNDFILE   OFF CACHE BOOL "" FORCE)

# ── C++ standard ─────────────────────────────────────────────────────────────
#
# Our top level sets CMAKE_CXX_STANDARD 20 as a NORMAL variable. sfizz sets 14
# as a CACHE variable. A normal variable wins over the cache, so without this
# save/restore sfizz and its vendored abseil would compile at 20 — a standard
# neither was tested against at this tag.
#
# 14 rather than 17 for a specific reason. SfizzConfig.cmake compiles a
# substitute global operator new/delete (utility/c++17/AlignedMemorySupport.cpp)
# when the standard is >= 17 AND its aligned-new probe fails — and that probe
# fails here, because sfizz forces CMAKE_OSX_DEPLOYMENT_TARGET to 10.9 while
# aligned allocation needs 10.14. Replacing global operator new inside a static
# library that is linked into a DAW-hosted plugin is not a thing to acquire by
# accident. At 14 that branch is unreachable.
#
# Safe because sfizz.hpp's public API is std::string, std::vector and
# std::unique_ptr only — no absl types, no std::string_view — so there is no
# type whose definition differs across the standard boundary.

set(KROMOSYNTH_SAVED_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD 14)

add_subdirectory("${KROMOSYNTH_SFIZZ_DIR}" EXCLUDE_FROM_ALL)

set(CMAKE_CXX_STANDARD "${KROMOSYNTH_SAVED_CXX_STANDARD}")

# ── clang 21 rejects ill-formed C++ in the vendored atomic_queue ────────────
#
# external/atomic_queue/include/atomic_queue/atomic_queue.h writes
#
#     Base::template do_pop_any(states_[index], elements_[index]);
#
# with no template argument list after the `template` keyword. That is
# ill-formed, and clang puts -Wmissing-template-arg-list-after-template-kw in
# its DefaultError group — so it is an error regardless of -Wall/-Werror, and
# sfizz's own warning settings have nothing to do with it. Four sites, hit by
# every TU that includes Synth.h.
#
# Checked before working around it: the same four lines are present verbatim at
# the development tip f5c6e29f as at 1.2.3. Moving the pin would NOT fix this.
# That is the argument for staying on the tag — a newer commit buys only the
# arm64 flag fix above and costs a tagged, auditable pin.
#
# Downgraded to a warning rather than patched, and applied per-target so our
# own code keeps the diagnostic at full strength. Probed rather than assumed,
# because the flag name is clang-version-specific.

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wno-error=missing-template-arg-list-after-template-kw"
                        KROMOSYNTH_HAVE_WNOERROR_TEMPLATE_KW)

if(KROMOSYNTH_HAVE_WNOERROR_TEMPLATE_KW)
    foreach(KROMOSYNTH_SFIZZ_TARGET IN ITEMS
            sfizz_internal sfizz_static sfizz_parser sfizz_messaging
            sfizz_spin_mutex sfizz_import)
        if(TARGET ${KROMOSYNTH_SFIZZ_TARGET})
            target_compile_options(${KROMOSYNTH_SFIZZ_TARGET} PRIVATE
                "-Wno-error=missing-template-arg-list-after-template-kw")
        endif()
    endforeach()
endif()

# ── Post-conditions ──────────────────────────────────────────────────────────

if(NOT TARGET sfizz::sfizz)
    message(FATAL_ERROR
        "sfizz configured but the target sfizz::sfizz does not exist. "
        "src/CMakeLists.txt links that name. At 1.2.3 it is an ALIAS for "
        "sfizz_static; check whether the alias was renamed.")
endif()

# The compile definition the SFIZZ_USE_SNDFILE=ON path sets, read off the
# target rather than off our own cache write. Note this must inspect
# sfizz_internal, not sfizz_static: sfizz_static links sfizz::internal
# PRIVATE, and it is sfizz_internal that carries the definition.
#
# Deliberately NOT a link-library check. st_audiofile is linked
# unconditionally, so grepping link libraries for "sndfile" matches on a
# perfectly clean build.
if(TARGET sfizz_internal)
    get_target_property(KROMOSYNTH_SFIZZ_INTERNAL_DEFS
                        sfizz_internal INTERFACE_COMPILE_DEFINITIONS)
    if(KROMOSYNTH_SFIZZ_INTERNAL_DEFS AND
       KROMOSYNTH_SFIZZ_INTERNAL_DEFS MATCHES "SFIZZ_USE_SNDFILE")
        message(FATAL_ERROR
            "sfizz_internal carries SFIZZ_USE_SNDFILE. libsndfile is LGPL and "
            "must not enter this build — see the session constraints.")
    endif()
else()
    message(WARNING
        "target sfizz_internal not found — the libsndfile assertion did not run")
endif()

message(STATUS "sfizz configured: static, dr_libs backend, no libsndfile, C++14")
