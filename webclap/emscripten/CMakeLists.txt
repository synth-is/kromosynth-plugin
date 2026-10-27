# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
#
# The Emscripten-side project of the skeleton WCLAP (module.wasm only: no ui.wasm yet). Configure with emcmake,
# via scripts/webclap-skeleton.sh. Modelled on the kit's ports/obxf (a JUCE processor in juce_wasm_dsp).

cmake_minimum_required(VERSION 3.20)
project(synthis-webclap LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure with emcmake (Emscripten toolchain)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(KIT_ROOT "${SYNTHIS_WEBCLAP_KIT}")
set(BUNDLE "${CMAKE_BINARY_DIR}/web/synthis.wclap")

set(JUCE_WEBCLAP_PLUGIN_DEFINITIONS
    JucePlugin_Name="Synth.is skeleton"
    JucePlugin_Desc="Synth.is"
    JucePlugin_Manufacturer="Synth.is"
    JucePlugin_VersionString="0.0.1"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_EditorRequiresKeyboardFocus=0)
include("${KIT_ROOT}/modules/juce_webclap/juce_webclap.cmake")

add_executable(module
    SkeletonProcessor.cpp
    "${KIT_ROOT}/modules/juce_webclap/juce_webclap_clap.cpp"
    "${KIT_ROOT}/modules/juce_webclap/juce_webclap_standalone.cpp")
target_include_directories(module PRIVATE "${KIT_ROOT}/external/clap/include")
target_compile_options(module PRIVATE -Wno-deprecated-declarations -Wno-macro-redefined)
target_link_libraries(module PRIVATE juce_wasm_dsp)
target_link_options(module PRIVATE ${JUCE_WEBCLAP_DSP_LINK_OPTIONS})
set_target_properties(module PROPERTIES SUFFIX ".wasm" RUNTIME_OUTPUT_DIRECTORY "${BUNDLE}")
