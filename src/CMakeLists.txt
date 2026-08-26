# ── Plugin target ────────────────────────────────────────────────────────────
#
# The juce_add_plugin call and its link/definition blocks are KEEPABLE.
# The source files it lists are not — see their headers.

juce_add_plugin(KromosynthPlugin
    PRODUCT_NAME             "Synth.is"
    VERSION                  ${PROJECT_VERSION}
    COMPANY_NAME             "Synth.is"
    COMPANY_WEBSITE          "https://synth.is"
    COMPANY_COPYRIGHT        "© 2026 Björn Þór Jónsson"
    BUNDLE_ID                "is.synth.plugin"

    # Manufacturer code must be exactly 4 chars with at least one uppercase,
    # or AU registration silently misbehaves.
    PLUGIN_MANUFACTURER_CODE Sthi
    PLUGIN_CODE              Krm0

    FORMATS                  AU VST3

    # IS_SYNTH TRUE even at Stage 0 with no audio: the format wrappers expose
    # different capabilities to the host depending on this, and changing it
    # later changes how Logic and Live categorise and re-scan the plugin. Set
    # it now to what Stage 1 needs so the Stage 0 host findings stay valid.
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE

    COPY_PLUGIN_AFTER_BUILD  TRUE
)

target_sources(KromosynthPlugin PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
)

kromosynth_configure_target(KromosynthPlugin)

target_compile_definitions(KromosynthPlugin PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0

    # Stage 0 deliberately does NOT enable native integration or a resource
    # provider. Both change the page's origin semantics — a resource provider
    # serves from JUCE's juce:// virtual origin, which would break the web
    # app's BrowserRouter (history-API routing, see packages/web/src/App.jsx)
    # and would make the cross-origin-isolation question unanswerable. We are
    # measuring the deployed origin as it actually is.
    KROMOSYNTH_WEB_APP_URL="${KROMOSYNTH_WEB_APP_URL}"
    KROMOSYNTH_WEBVIEW_PROBE=$<BOOL:${KROMOSYNTH_WEBVIEW_PROBE}>
    KROMOSYNTH_PROBE_DELAY_MS=${KROMOSYNTH_PROBE_DELAY_MS}
    KROMOSYNTH_EDITOR_STUB=$<BOOL:${KROMOSYNTH_EDITOR_STUB}>
    KROMOSYNTH_BROWSER_INSET=${KROMOSYNTH_BROWSER_INSET}
)

target_link_libraries(KromosynthPlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# LTO deliberately omitted at Stage 0: it slows the edit/build/host-test loop
# and this binary ships to nobody. Add juce::juce_recommended_lto_flags when
# Stage 1 starts producing signed builds.

if(KROMOSYNTH_BUILD_SAMPLER)
    target_link_libraries(KromosynthPlugin PRIVATE sfizz::sfizz)
    target_compile_definitions(KromosynthPlugin PRIVATE KROMOSYNTH_HAS_SAMPLER=1)
endif()
