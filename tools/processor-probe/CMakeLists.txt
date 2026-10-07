# processor-probe — developer tool, never shipped.
#
# KEEPABLE.
#
# Compiles the plugin's OWN processor, editor-less in use but not in build, into
# a console executable and drives processBlock with a fixed MIDI arpeggio. Every
# other gate here (faust_probe, engine_probe) links an engine directly, so none
# of them exercises the processor's parameter path, voice allocation under host
# MIDI, or the ADSR adoption on load -- which is how the polyphony regression of
# 2026-09-05 shipped. This is the gate that does.
#
# The source list and definitions mirror src/CMakeLists.txt on purpose: a probe
# built differently from the plugin reports on a different plugin.
#
# KROMOSYNTH_PROCESSOR_PROBE=1 lets InstrumentLibrary read a fixture library from
# $KROMOSYNTH_PROBE_LIBRARY; the shipped targets do not define it.

juce_add_console_app(processor_probe PRODUCT_NAME "processor-probe")

set(_src ${CMAKE_CURRENT_LIST_DIR}/../../src)

target_sources(processor_probe PRIVATE
    main.cpp
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
)

kromosynth_configure_target(processor_probe)

# The plugin's own sources are held to -Wall -Wextra -Wpedantic by
# kromosynth_configure_target; the probe's main.cpp is held to the same.
target_compile_definitions(processor_probe PRIVATE
    KROMOSYNTH_PROCESSOR_PROBE=1
    JUCE_STANDALONE_APPLICATION=1
    # The load finishes with a callAsync to the message thread, so main() has to
    # pump it (runDispatchLoopUntil).
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
    KROMOSYNTH_HAS_FAUST=1)

if(KROMOSYNTH_WASM3)
    target_link_libraries(processor_probe PRIVATE wasm3::wasm3)
    target_compile_definitions(processor_probe PRIVATE KROMOSYNTH_WASM3=1)
else()
    target_link_libraries(processor_probe PRIVATE wasmtime::wasmtime)
    target_compile_definitions(processor_probe PRIVATE KROMOSYNTH_WASM3=0)
endif()

target_link_libraries(processor_probe PRIVATE
    sfizz::sfizz
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
