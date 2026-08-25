# sfizz vendoring — Task 2 only, gated behind KROMOSYNTH_BUILD_SAMPLER.
#
# KEEPABLE.
#
# Not yet exercised: at the time this file was written the submodule had not
# been added, because Task 1 gates Task 2. The flag defaults OFF so a Task 1
# build never touches any of this.
#
# Constraints this encodes, from WP_SYNTHIS_PLUGIN.md §3 and the session brief:
#
#   • sfizz is BSD-2-Clause and embedded as a LIBRARY, not a process.
#   • Do NOT vendor libsndfile. sfizz's default audio file reader is dr_libs
#     (public domain / MIT-0), which sidesteps libsndfile's LGPL entirely.
#     SFIZZ_USE_SNDFILE=OFF below is the whole point; if a future sfizz
#     release flips that default, this line is what stops it silently.
#   • No extra dependencies beyond JUCE and sfizz.
#
# Maintenance risk, restated so it is not lost between documents: sfizz's last
# tagged library release is 1.2.3 (January 2024) and sfizz-ui was archived by
# its owner in June 2026. Pin to the tag; revisit at Stage 3; liquidsfz is the
# fallback. (WP §3, §9.)

set(KROMOSYNTH_SFIZZ_DIR "${CMAKE_SOURCE_DIR}/third_party/sfizz")

if(NOT EXISTS "${KROMOSYNTH_SFIZZ_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "KROMOSYNTH_BUILD_SAMPLER=ON but third_party/sfizz is empty.\n"
        "Add it as a pinned submodule first:\n"
        "    git submodule add https://github.com/sfztools/sfizz third_party/sfizz\n"
        "    git -C third_party/sfizz checkout 1.2.3\n"
        "    git submodule update --init --recursive")
endif()

# Build only the static library. Every other sfizz target is a liability here:
# the plugin wrappers would collide with our own AU/VST3 targets, the tools
# pull in extra deps, and the demos are irrelevant.
set(SFIZZ_SHARED        OFF CACHE BOOL "" FORCE)
set(SFIZZ_JACK          OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER        OFF CACHE BOOL "" FORCE)
set(SFIZZ_LV2           OFF CACHE BOOL "" FORCE)
set(SFIZZ_LV2_UI        OFF CACHE BOOL "" FORCE)
set(SFIZZ_VST           OFF CACHE BOOL "" FORCE)
set(SFIZZ_AU            OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS         OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS         OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS      OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS    OFF CACHE BOOL "" FORCE)

# The licence-critical one. dr_libs instead of libsndfile.
set(SFIZZ_USE_SNDFILE   OFF CACHE BOOL "" FORCE)

add_subdirectory("${KROMOSYNTH_SFIZZ_DIR}" EXCLUDE_FROM_ALL)

# Verify the toggle actually took effect rather than trusting that the option
# name still exists in this sfizz version. A renamed option would silently
# fall back to the default, and the default is the LGPL one.
if(SFIZZ_USE_SNDFILE)
    message(FATAL_ERROR
        "SFIZZ_USE_SNDFILE is ON after configuring sfizz. libsndfile is LGPL "
        "and must not enter this build — see the session constraints. Check "
        "whether the option was renamed in this sfizz version.")
endif()

message(STATUS "sfizz configured: static, dr_libs backend, no libsndfile")
