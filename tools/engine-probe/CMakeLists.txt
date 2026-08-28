# engine-probe — developer tool, never shipped.
#
# KEEPABLE.
#
# Links SamplerEngine directly, with juce_audio_basics and sfizz but no plugin
# wrapper and no audio device. That is the point: it can drive the engine
# through channel layouts no host on this machine can produce on demand, and
# neither auval nor pluginval offers a channel-configuration option.
#
# Same non-shipping conventions as sfz-probe: no COPY_AFTER_BUILD, no install
# rule. It measures the build tree it was built in.

juce_add_console_app(engine_probe PRODUCT_NAME "engine-probe")

target_sources(engine_probe PRIVATE
    main.cpp
    ../../src/SamplerEngine.cpp
)

# KROMOSYNTH_HAS_SAMPLER is not consulted by SamplerEngine.cpp itself -- the
# guard lives in PluginProcessor -- but the sfz path define is, so keep the two
# targets configured the same way to avoid a probe that measures a differently
# built engine than the plugin ships.
target_compile_definitions(engine_probe PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    KROMOSYNTH_HAS_SAMPLER=1
    KROMOSYNTH_SFZ_PATH="${KROMOSYNTH_SFZ_PATH}")

target_link_libraries(engine_probe PRIVATE
    sfizz::sfizz
    juce::juce_audio_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
