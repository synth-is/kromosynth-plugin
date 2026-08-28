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
    InstrumentLibrary.cpp
    InstrumentLibrary.h
    ShellPage.h
)

if(KROMOSYNTH_BUILD_SAMPLER)
    target_sources(KromosynthPlugin PRIVATE
        SamplerEngine.cpp
        SamplerEngine.h
    )
endif()

kromosynth_configure_target(KromosynthPlugin)

target_compile_definitions(KromosynthPlugin PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0

    # STAGE 1 (2026-08-28): native integration and a resource provider are now
    # ON for the shell component. The Stage 0 reasoning below was correct and
    # is kept because it explains what changed and why it was safe to change.
    #
    #   Stage 0 deliberately did NOT enable either. Both change the page's
    #   origin semantics — a resource provider serves from JUCE's virtual
    #   origin, which would break the web app's BrowserRouter (history-API
    #   routing, see packages/web/src/App.jsx) and would make the
    #   cross-origin-isolation question unanswerable. We were measuring the
    #   deployed origin as it actually is.
    #
    # That objection still stands — and is precisely why there are now TWO
    # components. The shell is served from the virtual origin and carries the
    # bridge; the site keeps the deployed origin, its router and its isolation
    # semantics, with native integration OFF. Neither compromises the other.
    # See WP_SYNTHIS_PLUGIN.md §12.
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

# KROMOSYNTH_HAS_SAMPLER is defined in BOTH configurations, 1 or 0, rather than
# defined-or-absent. The sources use `#if KROMOSYNTH_HAS_SAMPLER`, and an
# undefined identifier there is silently 0 — which is correct C++ and a bad
# place to rely on silence.
if(KROMOSYNTH_BUILD_SAMPLER)
    target_link_libraries(KromosynthPlugin PRIVATE sfizz::sfizz)
    target_compile_definitions(KromosynthPlugin PRIVATE
        KROMOSYNTH_HAS_SAMPLER=1
        KROMOSYNTH_SFZ_PATH="${KROMOSYNTH_SFZ_PATH}")
    # Configure-time, because the runtime symptom of a typo'd path is an
    # instrument that loads and plays silence — the same symptom as F11's
    # culled-region case, and indistinguishable from it in a DAW. Catch it
    # where it is cheap.
    #
    # Empty stays legal: it means "engine built, nothing loaded".
    if(KROMOSYNTH_SFZ_PATH AND NOT EXISTS "${KROMOSYNTH_SFZ_PATH}")
        message(FATAL_ERROR
            "KROMOSYNTH_SFZ_PATH points at a file that does not exist:\n"
            "    ${KROMOSYNTH_SFZ_PATH}\n"
            "Note the two on-disk artefact layouts recorded in "
            "STAGE0_FINDINGS.md E5: the hub uses ARTIFACT_STORAGE_PATH with "
            "anonymous/<artifactId>, the local dev tree uses "
            "<runId>/<artifactId>.")
    endif()
else()
    target_compile_definitions(KromosynthPlugin PRIVATE KROMOSYNTH_HAS_SAMPLER=0)
endif()

# Must come AFTER juce_add_plugin, because it appends POST_BUILD steps that
# have to run after JUCE's own ad-hoc sign and copy. See cmake/Signing.cmake.
kromosynth_sign_plugin_targets(KromosynthPlugin)
