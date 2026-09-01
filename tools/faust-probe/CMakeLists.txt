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

target_link_libraries(faust_probe PRIVATE
    wasmtime::wasmtime
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
