# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# wclap-host-probe (synthis-et4.3) — developer tool, never shipped, only with SYNTHIS_WCLAP_HOST=ON.
#
# Drives the plugin's OWN KromosynthAudioProcessor with a hosted WCLAP: load through InstrumentLibrary /
# SubstrateRouting, a note through processBlock's MidiBuffer, clap.state round trips (same instance and
# a fresh processor), and CPU per voice. Source list and definitions mirror tools/processor-probe on
# purpose, plus WclapEngine and the bridge.

juce_add_console_app(wclap_host_probe PRODUCT_NAME "wclap-host-probe")

set(_src ${CMAKE_CURRENT_LIST_DIR}/../../src)

target_sources(wclap_host_probe PRIVATE
    main.cpp
    WebViewSnapshot.mm
    ${_src}/PluginProcessor.cpp
    ${_src}/PluginEditor.cpp
    ${_src}/InstrumentLibrary.cpp
    ${_src}/ArtifactAcquisition.cpp
    ${_src}/AcquisitionRunner.cpp
    ${_src}/RenderOutbox.cpp
    ${_src}/RenderList.cpp
    ${_src}/ExternalBrowser.cpp
    ${_src}/SamplerEngine.cpp
    ${_src}/FaustEngine.cpp
    ${_src}/WclapEngine.cpp
    ${_src}/WclapEngineView.cpp
)

kromosynth_configure_target(wclap_host_probe)

target_compile_definitions(wclap_host_probe PRIVATE
    KROMOSYNTH_PROCESSOR_PROBE=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1
    KROMOSYNTH_MEASURE_EPOCH=${KROMOSYNTH_MEASURE_EPOCH}
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    KROMOSYNTH_WEB_APP_URL="${KROMOSYNTH_WEB_APP_URL}"
    KROMOSYNTH_VI_SERVICE_URL="${KROMOSYNTH_VI_SERVICE_URL}"
    KROMOSYNTH_APP_GROUP_ID="${KROMOSYNTH_APP_GROUP_ID}"
    KROMOSYNTH_WEBVIEW_PROBE=0
    KROMOSYNTH_PROBE_DELAY_MS=${KROMOSYNTH_PROBE_DELAY_MS}
    KROMOSYNTH_EDITOR_STUB=$<BOOL:${KROMOSYNTH_EDITOR_STUB}>
    KROMOSYNTH_BROWSER_INSET=${KROMOSYNTH_BROWSER_INSET}
    KROMOSYNTH_HAS_SAMPLER=1
    KROMOSYNTH_SFZ_PATH="${KROMOSYNTH_SFZ_PATH}"
    KROMOSYNTH_HAS_FAUST=1
    KROMOSYNTH_HAS_WCLAP=1
    KROMOSYNTH_WCLAP_NATIVE_REFERENCE=1
    KROMOSYNTH_WASM3=0)

target_link_libraries(wclap_host_probe PRIVATE
    wasmtime::wasmtime
    kromosynth_wclap_bridge
    sfizz::sfizz
    juce::juce_audio_utils
    juce::juce_gui_extra
    "-framework WebKit"
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
