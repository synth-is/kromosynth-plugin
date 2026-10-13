# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# faust-probe — developer tool, never shipped.
#
# KEEPABLE.
#
# Links FaustEngine directly, with juce_audio_basics and wasmtime but no plugin
# wrapper, no webview and no audio device. Same reason engine-probe exists: the
# failure this engine is most likely to have is silence, and silence passes
# every validator. auval's render tests only check that rendering does not
# crash.
#
# Same non-shipping conventions as sfz-probe and engine-probe: no
# COPY_AFTER_BUILD, no install rule.

juce_add_console_app(faust_probe PRODUCT_NAME "faust-probe")

target_sources(faust_probe PRIVATE
    main.cpp
    ../../src/FaustEngine.cpp
)

target_compile_definitions(faust_probe PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

# The probe follows the plugin's runtime choice, because it IS the acceptance
# test for that choice: scripts/null-test.sh drives faust_probe over 120 elites
# and compares samples against the platform reference. A probe pinned to
# wasmtime while the plugin used wasm3 would report on the wrong engine and
# pass while the shipped one diverged.
if(KROMOSYNTH_WASM3)
    target_link_libraries(faust_probe PRIVATE wasm3::wasm3)
    target_compile_definitions(faust_probe PRIVATE KROMOSYNTH_WASM3=1)
else()
    target_link_libraries(faust_probe PRIVATE wasmtime::wasmtime)
    target_compile_definitions(faust_probe PRIVATE KROMOSYNTH_WASM3=0)
endif()

target_link_libraries(faust_probe PRIVATE
    juce::juce_audio_basics
    # FaustEngine::measure builds a spectrogram with juce::dsp::FFT. The probe
    # got away without this while nothing here called measure() -- LTO simply
    # stripped the unreferenced code -- and the click test calls it.
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
