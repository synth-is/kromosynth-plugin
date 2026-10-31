# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
#
# synthis-et4.9: the Synth.is plugin as a WCLAP. Added only when SYNTHIS_WEBCLAP_TARGET=ON (top-level
# CMakeLists.txt); nothing here is configured otherwise.
#
# The WCLAP is a different toolchain (Emscripten, wasm32), so it cannot be a target in this build tree. This
# directory therefore does two things: a custom target `synthis_webclap` for the native tree that runs
# scripts/webclap-skeleton.sh (emcmake configure of webclap/emscripten, build, bundle), and, in
# webclap/emscripten/, the Emscripten-side project itself. See docs/PLUGIN_AS_WCLAP_2026-10-27.md.

set(SYNTHIS_WEBCLAP_KIT "$ENV{HOME}/wclap-native/open-webclap" CACHE PATH
    "open-webclap checkout (the juce_webclap kit, its JUCE, CLAP headers); pinned 0833f5b")
set(SYNTHIS_WEBCLAP_EMSDK "$ENV{HOME}/wclap-native/emsdk" CACHE PATH
    "emsdk directory; the kit is tested with Emscripten 3.1.51 (legacy exceptions)")
set(SYNTHIS_WEBCLAP_BUILD_DIR "${CMAKE_BINARY_DIR}/webclap" CACHE PATH "Where the bundle is built")
option(SYNTHIS_WEBCLAP_EXNREF "Build with exnref wasm exceptions (needs Emscripten >= 4.0.7, e.g. emsdk-4)" OFF)

if(NOT EXISTS "${SYNTHIS_WEBCLAP_KIT}/modules/juce_webclap/juce_webclap.cmake")
    message(FATAL_ERROR "SYNTHIS_WEBCLAP_TARGET=ON needs the open-webclap kit: set -DSYNTHIS_WEBCLAP_KIT=<checkout> "
                        "(not found: ${SYNTHIS_WEBCLAP_KIT}). Run its scripts/fetch-deps.py once.")
endif()
if(NOT EXISTS "${SYNTHIS_WEBCLAP_EMSDK}/emsdk_env.sh")
    message(FATAL_ERROR "SYNTHIS_WEBCLAP_TARGET=ON needs an emsdk: set -DSYNTHIS_WEBCLAP_EMSDK=<dir> "
                        "(no emsdk_env.sh in ${SYNTHIS_WEBCLAP_EMSDK}).")
endif()

add_custom_target(synthis_webclap
    COMMAND "${CMAKE_COMMAND}" -E env
            "SYNTHIS_WEBCLAP_KIT=${SYNTHIS_WEBCLAP_KIT}" "SYNTHIS_WEBCLAP_EMSDK=${SYNTHIS_WEBCLAP_EMSDK}"
            "SYNTHIS_WEBCLAP_BUILD_DIR=${SYNTHIS_WEBCLAP_BUILD_DIR}" "SYNTHIS_WEBCLAP_EXNREF=${SYNTHIS_WEBCLAP_EXNREF}"
            bash "${PROJECT_SOURCE_DIR}/scripts/webclap-skeleton.sh" build
    COMMENT "Building the skeleton WCLAP (Emscripten, out of tree) into ${SYNTHIS_WEBCLAP_BUILD_DIR}"
    VERBATIM USES_TERMINAL)
