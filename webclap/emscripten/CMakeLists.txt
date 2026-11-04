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

# Which processor the module is built around:
#   skeleton  SkeletonProcessor.cpp, a sine voice (synthis-et4.9)
#   faust     FaustProcessor.cpp: src/FaustEngine.cpp on wasm3 compiled into the module, with one Faust artefact
#             embedded (SYNTHIS_WEBCLAP_FAUST_ARTEFACT, a path without extension) (synthis-et4.17)
#   sampler   SamplerProcessor.cpp: src/SamplerEngine.cpp on sfizz (no threads) inside the module, with a generated
#             test SFZ + WAV embedded (SYNTHIS_WEBCLAP_SAMPLER_DATA, the directory holding them) (synthis-et4.18)
set(SYNTHIS_WEBCLAP_PROCESSOR "skeleton" CACHE STRING "skeleton | faust | sampler")
set_property(CACHE SYNTHIS_WEBCLAP_PROCESSOR PROPERTY STRINGS skeleton faust sampler)
set(SYNTHIS_PLUGIN_NAME "Synth.is skeleton")
set(SYNTHIS_PROCESSOR_SOURCES SkeletonProcessor.cpp)
set(SYNTHIS_PROCESSOR_LIBS "")
set(SYNTHIS_PROCESSOR_LINK_OPTIONS "")
set(SYNTHIS_STANDALONE_SHIM "${KIT_ROOT}/modules/juce_webclap/juce_webclap_standalone.cpp")
if(SYNTHIS_WEBCLAP_PROCESSOR STREQUAL "faust")
    set(SYNTHIS_PLUGIN_NAME "Synth.is Faust spike")
elseif(SYNTHIS_WEBCLAP_PROCESSOR STREQUAL "sampler")
    set(SYNTHIS_PLUGIN_NAME "Synth.is sampler spike")
endif()

set(JUCE_WEBCLAP_PLUGIN_DEFINITIONS
    JucePlugin_Name="${SYNTHIS_PLUGIN_NAME}"
    JucePlugin_Desc="Synth.is"
    JucePlugin_Manufacturer="Synth.is"
    JucePlugin_VersionString="0.0.1"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_EditorRequiresKeyboardFocus=0)
include("${KIT_ROOT}/modules/juce_webclap/juce_webclap.cmake")

if(SYNTHIS_WEBCLAP_PROCESSOR STREQUAL "faust")
    get_filename_component(PLUGIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    if(NOT SYNTHIS_WEBCLAP_FAUST_ARTEFACT)
        message(FATAL_ERROR "SYNTHIS_WEBCLAP_PROCESSOR=faust needs -DSYNTHIS_WEBCLAP_FAUST_ARTEFACT=<dir>/<stem> "
                            "(<stem>.wasm, .json and .plan, e.g. a Gate 0 elite; see scripts/webclap-faust.sh)")
    endif()
    include(EmbedArtefact.cmake)
    synthis_embed_faust_artefact("${SYNTHIS_WEBCLAP_FAUST_ARTEFACT}" "${CMAKE_BINARY_DIR}/gen/synthis_faust_artefact.h")

    # wasm3 (MIT), the copy the repo already vendors for iOS Gate 0 and ships a licence for (tools/ios-gate0/wasm3,
    # 0.9.1-beta.1 with local patches). Same source set as cmake/Wasm3.cmake: the WASI and tracer shims excluded.
    set(SYNTHIS_WASM3_DIR "${PLUGIN_ROOT}/tools/ios-gate0/wasm3" CACHE PATH "wasm3 sources")
    file(GLOB _w3 "${SYNTHIS_WASM3_DIR}/*.c")
    list(FILTER _w3 EXCLUDE REGEX "m3_api_(uvwasi|meta_wasi|tracer|wasi)\\.c$")
    add_library(synthis_wasm3 STATIC ${_w3})
    target_include_directories(synthis_wasm3 PUBLIC "${SYNTHIS_WASM3_DIR}")
    # d_m3MaxNativeStack: wasm3's guard budget defaults to 8 MB - 128 KB of native stack, more than the kit's
    # STACK_SIZE (1 MB). On wasm32 the limit (stack pointer - budget) then wraps below address 0 and every call
    # traps "stack overflow" at init. 768 KB keeps the guard inside the module's 1 MB shadow stack.
    target_compile_definitions(synthis_wasm3 PUBLIC d_m3HasWASI=0 "d_m3MaxNativeStack=(768*1024)")
    # Tail calls: wasm3's dispatch is musttail-threaded; on wasm32 that needs the tail-call proposal
    # (return_call), which Node 22 / V8 has. Without it every op would nest a native call. OFF to measure the
    # difference (M3_HAS_TAIL_CALL=0, a plain call chain).
    option(SYNTHIS_WASM3_TAIL_CALL "Build wasm3 with -mtail-call (musttail dispatch)" ON)
    if(SYNTHIS_WASM3_TAIL_CALL)
        target_compile_options(synthis_wasm3 PUBLIC -mtail-call)
        set(SYNTHIS_PROCESSOR_LINK_OPTIONS -mtail-call)
    else()
        target_compile_definitions(synthis_wasm3 PUBLIC M3_HAS_TAIL_CALL=0)
    endif()
    target_compile_options(synthis_wasm3 PRIVATE ${JUCE_WEBCLAP_FLAGS} -O3
        -Wno-unused-function -Wno-unused-variable -Wno-strict-prototypes)
    set_target_properties(synthis_wasm3 PROPERTIES C_STANDARD 11)

    # FaustEngine includes juce_dsp (for the FFT in measure()), which the kit puts only in its GUI library;
    # juce_dsp.cpp in turn includes juce_audio_formats. Both on top of the GUI-free juce_wasm_dsp.
    add_library(synthis_juce_dsp STATIC
        "${JUCE_MODULES}/juce_audio_formats/juce_audio_formats.cpp"
        "${JUCE_MODULES}/juce_dsp/juce_dsp.cpp")
    target_compile_definitions(synthis_juce_dsp PUBLIC
        JUCE_MODULE_AVAILABLE_juce_audio_formats=1 JUCE_MODULE_AVAILABLE_juce_dsp=1 JUCE_USE_OGGVORBIS=0)
    target_link_libraries(synthis_juce_dsp PUBLIC juce_wasm_dsp)
    target_compile_options(synthis_juce_dsp PRIVATE -w)

    set(SYNTHIS_PROCESSOR_SOURCES FaustProcessor.cpp "${PLUGIN_ROOT}/src/FaustEngine.cpp")
    set(SYNTHIS_PROCESSOR_LIBS synthis_wasm3 synthis_juce_dsp)
    set(BUNDLE "${CMAKE_BINARY_DIR}/web/synthis-faust.wclap")
endif()

if(SYNTHIS_WEBCLAP_PROCESSOR STREQUAL "sampler")
    get_filename_component(PLUGIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    if(NOT SYNTHIS_WEBCLAP_SAMPLER_DATA OR NOT EXISTS "${SYNTHIS_WEBCLAP_SAMPLER_DATA}/test.sfz")
        message(FATAL_ERROR "SYNTHIS_WEBCLAP_PROCESSOR=sampler needs -DSYNTHIS_WEBCLAP_SAMPLER_DATA=<dir> holding test.sfz "
                            "and tone.wav (node scripts/webclap-sampler-data.mjs <dir>; scripts/webclap-sampler.sh does it)")
    endif()
    include(EmbedFiles.cmake)
    synthis_embed_files("${CMAKE_BINARY_DIR}/gen/synthis_sampler_files.h"
        "${SYNTHIS_WEBCLAP_SAMPLER_DATA}/test.sfz" "${SYNTHIS_WEBCLAP_SAMPLER_DATA}/tone.wav")
    add_subdirectory(sfizz)   # sfizz::sfizz, patched for no threads

    set(SYNTHIS_PROCESSOR_SOURCES SamplerProcessor.cpp "${PLUGIN_ROOT}/src/SamplerEngine.cpp")
    set(SYNTHIS_PROCESSOR_LIBS sfizz::sfizz)
    # WasmFS: the in-memory file system sfizz and juce::File read from, inside a standalone module (the JS MEMFS
    # needs Emscripten's JS glue, which a WCLAP does not have).
    set(SYNTHIS_PROCESSOR_LINK_OPTIONS -sWASMFS=1)
    set(SYNTHIS_STANDALONE_SHIM StandaloneWasmfs.cpp)   # the kit's shim stubs the file syscalls WasmFS defines
    set(BUNDLE "${CMAKE_BINARY_DIR}/web/synthis-sampler.wclap")
endif()

add_executable(module
    ${SYNTHIS_PROCESSOR_SOURCES}
    "${KIT_ROOT}/modules/juce_webclap/juce_webclap_clap.cpp"
    "${SYNTHIS_STANDALONE_SHIM}")
target_include_directories(module PRIVATE "${KIT_ROOT}/external/clap/include")
target_compile_options(module PRIVATE -Wno-deprecated-declarations -Wno-macro-redefined)
target_link_libraries(module PRIVATE juce_wasm_dsp ${SYNTHIS_PROCESSOR_LIBS})
target_link_options(module PRIVATE ${JUCE_WEBCLAP_DSP_LINK_OPTIONS} ${SYNTHIS_PROCESSOR_LINK_OPTIONS})
set_target_properties(module PROPERTIES SUFFIX ".wasm" RUNTIME_OUTPUT_DIRECTORY "${BUNDLE}")
if(SYNTHIS_WEBCLAP_PROCESSOR STREQUAL "faust")
    target_include_directories(module PRIVATE "${PLUGIN_ROOT}/src" "${CMAKE_BINARY_DIR}/gen")
    target_compile_definitions(module PRIVATE KROMOSYNTH_WASM3=1)
elseif(SYNTHIS_WEBCLAP_PROCESSOR STREQUAL "sampler")
    target_include_directories(module PRIVATE "${PLUGIN_ROOT}/src" "${CMAKE_BINARY_DIR}/gen")
endif()
