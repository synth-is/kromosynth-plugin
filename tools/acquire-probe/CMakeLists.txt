# acquire-probe — developer tool, never shipped.
#
# KEEPABLE.
#
# Links ArtifactAcquisition with juce_core only: no plugin wrapper, no webview,
# no audio device, no sfizz. That is what lets the acquisition path be exercised
# without building the plugin — which matters more here than convenience,
# because a plugin rebuild re-signs and un-notarises the installed bundle
# (STAGE0_FINDINGS L5, L7).
#
# NOT gated behind KROMOSYNTH_BUILD_SAMPLER, unlike sfz-probe and engine-probe.
# Those link sfizz and have to be. This one does not, and requiring the sampler
# to test a downloader would be a dependency invented for symmetry.
#
# Same non-shipping conventions as the other probes: no COPY_AFTER_BUILD, no
# install rule.

juce_add_console_app(acquire_probe PRODUCT_NAME "acquire-probe")

target_sources(acquire_probe PRIVATE
    main.cpp
    ../../src/ArtifactAcquisition.cpp
    ../../src/InstrumentLibrary.cpp
    # ArtifactAcquisition calls renders::fileNameFor for the `render` verb, so
    # this is a link dependency and not an optional extra. Adding it also keeps
    # the probe able to exercise both verbs, which is the point of the probe.
    ../../src/RenderOutbox.cpp
)

# JUCE_WEB_BROWSER=0 is honest rather than incidental: nothing in
# ArtifactAcquisition touches a webview, and the probe would be a poorer
# stand-in for the plugin if it needed one to link.
#
# JUCE_USE_CURL=0 keeps the HTTP path on NSURLSession, which is what the plugin
# uses. A probe that reached the network through a different stack than the
# thing it stands in for would not be measuring it (STAGE0_FINDINGS I2).
target_compile_definitions(acquire_probe PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

target_link_libraries(acquire_probe PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
