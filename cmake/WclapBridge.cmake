# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
#
# SYNTHIS_WCLAP_HOST (synthis-et4.3, spike): link the WebCLAP wclap-bridge into the plugin so it can
# host a WCLAP (a CLAP plugin compiled to WebAssembly) such as the Surge XT bundle. Default OFF.
#
# ONE WASMTIME. The bridge's own CMake fetches Wasmtime v39.0.1; this file does not use it. The bridge
# sources are compiled here against `wasmtime::wasmtime` from cmake/Wasmtime.cmake (the plugin's pin,
# 48.0.1), so FaustEngine and the bridge share a single libwasmtime.a. Do not add_subdirectory() the
# bridge's CMakeLists.
#
# The bridge is fetched at configure time and patched with cmake/wclap-bridge/synthis-bridge.patch:
#   1. hostTemplate zero-initialised and given clap_version (otherwise create_plugin returns null at
#      random: docs/PLUGIN_AS_HOST_SPIKE.md finding 4, docs/WCLAP_BRIDGE_UPSTREAM_ISSUE.md s.1)
#   2. wtCall refuses a null funcref instead of letting Wasmtime panic (finding 5, issue s.2)
#   3. wasm exceptions enabled in the Wasmtime config (the exnref Surge XT bundle needs it)
#   4. wasi_config_preopen_dir ported to the Wasmtime 48 signature (issue s.3)
# The per-call time limit is a runtime argument to wclap_global_init (see WclapEngine.cpp), not a patch.
#
# BSL-1.0 (bridge) / GPL-3.0 (the Surge XT bundle it loads). The plugin is AGPL-3.0, so a plugin built
# with this option carries the bridge but NOT any WCLAP: bundles are data, installed separately.

include(FetchContent)

set(KROMOSYNTH_WCLAP_BRIDGE_REV "cd11d22afbe2af350f24cd56e6e0536e5ca86452")

if(KROMOSYNTH_IOS)
    message(FATAL_ERROR "SYNTHIS_WCLAP_HOST is macOS (and later Windows) only: Wasmtime needs a JIT, "
                        "which iOS does not permit.")
endif()
if(NOT KROMOSYNTH_BUILD_FAUST OR KROMOSYNTH_WASM3)
    message(FATAL_ERROR "SYNTHIS_WCLAP_HOST needs the wasmtime runtime: configure with "
                        "-DKROMOSYNTH_BUILD_FAUST=ON and without KROMOSYNTH_WASM3.")
endif()
if(NOT APPLE)
    message(FATAL_ERROR "SYNTHIS_WCLAP_HOST is implemented for macOS only so far (webview-gui needs "
                        "WebKit; the Windows path is untested).")
endif()

FetchContent_Declare(wclap_bridge_src
    GIT_REPOSITORY https://github.com/WebCLAP/wclap-bridge
    GIT_TAG        ${KROMOSYNTH_WCLAP_BRIDGE_REV}
    GIT_SUBMODULES_RECURSE ON
    GIT_PROGRESS   OFF
    PATCH_COMMAND  git apply --whitespace=nowarn
                   "${CMAKE_CURRENT_LIST_DIR}/wclap-bridge/synthis-bridge.patch"
    UPDATE_DISCONNECTED ON)
FetchContent_GetProperties(wclap_bridge_src)
if(NOT wclap_bridge_src_POPULATED)
    # FetchContent_Populate(<name>) with only a name is the pre-3.30 form; MakeAvailable would run the
    # bridge's CMakeLists, which is exactly what must not happen.
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(wclap_bridge_src)
    cmake_policy(POP)
endif()
set(_wb "${wclap_bridge_src_SOURCE_DIR}")

add_library(kromosynth_wclap_bridge STATIC
    ${_wb}/source/wclap-bridge.cpp
    ${_wb}/source/wclap-instance-wasmtime/wclap-instance-wasmtime.cpp
    ${_wb}/modules/webview-gui/source/webview-gui.cpp)
target_include_directories(kromosynth_wclap_bridge
    PUBLIC  ${_wb}/include ${_wb}/modules/clap/include
    PRIVATE ${_wb}/modules/wclap-cpp/include ${_wb}/modules/webview-gui/include ${_wb}/modules/choc)
target_compile_features(kromosynth_wclap_bridge PUBLIC cxx_std_17)
target_link_libraries(kromosynth_wclap_bridge PRIVATE wasmtime::wasmtime
    "-framework WebKit" "-framework Cocoa")
target_include_directories(kromosynth_wclap_bridge PRIVATE ${_wb}/source)
# Third-party code: not held to the plugin's warning level.
target_compile_options(kromosynth_wclap_bridge PRIVATE -w)
set_target_properties(kromosynth_wclap_bridge PROPERTIES POSITION_INDEPENDENT_CODE ON)
