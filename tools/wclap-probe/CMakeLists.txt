# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# wclap-probe (synthis-aa4.23 spike). Standalone: NOT part of the plugin build or the gate.
# It links a separately built wclap-bridge, which brings its own Wasmtime (v39.0.1; the
# plugin pins 48.0.1 -- see docs/PLUGIN_AS_HOST_SPIKE.md). Build: scripts/wclap-spike.sh
cmake_minimum_required(VERSION 3.20)
project(wclap_probe CXX)
set(CMAKE_CXX_STANDARD 17)
set(WCLAP_BRIDGE_DIR "" CACHE PATH "wclap-bridge checkout, already built in <dir>/build")
add_executable(wclap-probe main.cpp)
target_include_directories(wclap-probe PRIVATE ${WCLAP_BRIDGE_DIR}/include ${WCLAP_BRIDGE_DIR}/modules/clap/include)
target_link_libraries(wclap-probe PRIVATE
  ${WCLAP_BRIDGE_DIR}/build/libwclap-bridge.a
  ${WCLAP_BRIDGE_DIR}/build/modules/webview-gui/libwebview-gui.a
  ${WCLAP_BRIDGE_DIR}/build/libwasmtime-combined.a
  "-framework Cocoa" "-framework WebKit" "-framework Security" "-framework CoreFoundation")
