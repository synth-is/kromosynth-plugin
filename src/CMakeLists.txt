# ── Plugin target ────────────────────────────────────────────────────────────
#
# The juce_add_plugin call and its link/definition blocks are KEEPABLE.
# The source files it lists are not — see their headers.

# Formats are per-platform, and the iOS pair is not a subset of the macOS pair.
#
# On iOS, Standalone is not an optional extra: JUCE builds the AUv3 as an app
# EXTENSION, and an extension needs a container app to live inside. The
# standalone app is therefore the delivery vehicle for the plugin rather than a
# separate product, which is why standalone-first is the cheapest route to a
# working AUv3 rather than a detour away from one.
#
# AUv3 is deliberately NOT enabled yet. It brings its own sandbox: an extension
# cannot read the container app's Documents directory, so InstrumentLibrary and
# the acquisition session both have to move to an App Group container first.
# Adding the format before that work exists would produce an AUv3 that loads
# and then finds an empty instrument library.
if(KROMOSYNTH_IOS)
    set(_kromosynth_formats Standalone)
else()
    set(_kromosynth_formats AU VST3)
endif()

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

    FORMATS                  ${_kromosynth_formats}

    # IS_SYNTH TRUE even at Stage 0 with no audio: the format wrappers expose
    # different capabilities to the host depending on this, and changing it
    # later changes how Logic and Live categorise and re-scan the plugin. Set
    # it now to what Stage 1 needs so the Stage 0 host findings stay valid.
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE

    # iOS suspends an app's audio when it leaves the foreground unless the
    # background-audio capability is declared. A synth that stops when the
    # screen locks is not a synth. On a native target this is one Info.plist
    # key, which is one of the concrete reasons the native route beat running
    # the engine inside a WKWebView.
    #
    # The keyword is BACKGROUND_AUDIO_ENABLED. BACKGROUND_AUDIO is not a JUCE
    # argument at all — it is absent from the one-value list in
    # JUCEUtils.cmake around line 2072 — so juce_add_plugin silently ignored
    # the shorter spelling and the first iOS build shipped with an empty
    # UIBackgroundModes. Same class of mistake as JUCE_FORCE_LEGACY_PARAM_IDS
    # below, and only findable by reading the BUILT Info.plist.
    #
    # WHICH BRINGS THE TRAP WORTH RECORDING: with the Xcode generator,
    # `xcodebuild -target <name>` does NOT depend on ZERO_CHECK, so a changed
    # CMakeLists is not picked up and the plist read back is the PREVIOUS
    # build's. Two verifications of this very block were invalid for that
    # reason and produced a confident, wrong diagnosis — that CMake's `ON` was
    # not a plist boolean. Re-run `cmake -S . -B <dir>` before trusting any
    # plist read after a build-file edit. `ON` is fine; measured.
    #
    # FILE_SHARING_ENABLED so instruments and logs can be pulled off the device
    # without a debugger attached.
    BACKGROUND_AUDIO_ENABLED ${KROMOSYNTH_IOS}
    FILE_SHARING_ENABLED     ${KROMOSYNTH_IOS}
    REQUIRES_FULL_SCREEN     ${KROMOSYNTH_IOS}

    # No microphone anywhere in the signal path. Declared explicitly because
    # leaving it unset makes JUCE emit the usage-description key on iOS, and an
    # app that asks for a microphone it never opens is one App Review question
    # nobody needs to answer.
    MICROPHONE_PERMISSION_ENABLED FALSE

    COPY_PLUGIN_AFTER_BUILD  ${KROMOSYNTH_COPY_AFTER_BUILD}
)

target_sources(KromosynthPlugin PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
    InstrumentLibrary.cpp
    InstrumentLibrary.h
    ArtifactAcquisition.cpp
    ArtifactAcquisition.h
    AcquisitionRunner.cpp
    AcquisitionRunner.h
    ShellPage.h
)

if(KROMOSYNTH_BUILD_SAMPLER)
    target_sources(KromosynthPlugin PRIVATE
        SamplerEngine.cpp
        SamplerEngine.h
    )
endif()

if(KROMOSYNTH_BUILD_FAUST)
    target_sources(KromosynthPlugin PRIVATE
        FaustEngine.cpp
        FaustEngine.h
    )
endif()

kromosynth_configure_target(KromosynthPlugin)

# PUBLIC, and the visibility is half the point; the spelling is the other half.
#
# AudioUnit parameter ids become the parameter INDEX rather than a hash of the
# string id. Without it, juce_audio_plugin_client_AU_1.mm derives the id as
# juceParamID.hashCode() -- "decay" becomes 95459258 -- and the AU reports its
# parameter list sorted by that id, so a host shows an order nobody chose.
# GarageBand listed them Decay, Sustain, Attack, Release, Output against an
# ADSR ordering that is the whole convention for these four controls. auval
# confirms both the ids and the order.
#
# TWO ways this silently did nothing before it worked, both worth keeping:
#
#   1. It was PRIVATE. The AU wrapper is compiled into the FORMAT target --
#      KromosynthPlugin_AU -- which links the shared-code target rather than
#      being it, so a PRIVATE definition never reaches the file that reads the
#      flag.
#   2. It was spelled JUCE_FORCE_LEGACY_PARAM_IDS. The macro the wrapper tests
#      is JUCE_FORCE_USE_LEGACY_PARAM_IDS. A misspelled feature macro is not an
#      error anywhere: it compiles, links, installs and does nothing.
#
# Neither produced a warning. The only symptom either time was a knob order
# that stayed wrong, which is why this was verified with auval rather than by
# looking at the source and believing it.
#
# The cost is that every parameter id changes, so automation and saved parameter
# state from an earlier build will not map. Free while these parameters are
# hours old and the plugin is unreleased; not free after.
target_compile_definitions(KromosynthPlugin PUBLIC
    JUCE_FORCE_USE_LEGACY_PARAM_IDS=1)

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
    KROMOSYNTH_VI_SERVICE_URL="${KROMOSYNTH_VI_SERVICE_URL}"
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
        # For juce::dsp::FFT, used to turn the measurement render into a
        # spectrogram. On Apple platforms it wraps vDSP, so this is both the
        # correct choice and the fast one -- and hand-rolling numerics when the
        # already-vendored library has a vetted implementation would be a poor
        # trade for one call site.
        juce::juce_dsp
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

# Same 1-or-0 convention as KROMOSYNTH_HAS_SAMPLER above, for the same reason:
# `#if` on an undefined identifier is silently 0, which is correct C++ and a bad
# place to rely on silence.
if(KROMOSYNTH_BUILD_FAUST)
    target_compile_definitions(KromosynthPlugin PRIVATE KROMOSYNTH_HAS_FAUST=1)

    # EXACTLY ONE runtime is linked. An earlier version linked wasmtime always,
    # on the theory that FaustEngine.cpp still names its types in the branch the
    # preprocessor drops. It does not -- every wasmtime reference now lives
    # inside `#if ! KROMOSYNTH_WASM3`, including the headers -- and linking both
    # put a MACOS libwasmtime.a into an iOS link, which failed for a reason
    # entirely unrelated to what was being tested.
    if(KROMOSYNTH_WASM3)
        target_link_libraries(KromosynthPlugin PRIVATE wasm3::wasm3)
        target_compile_definitions(KromosynthPlugin PRIVATE KROMOSYNTH_WASM3=1)
    else()
        target_link_libraries(KromosynthPlugin PRIVATE wasmtime::wasmtime)
        target_compile_definitions(KromosynthPlugin PRIVATE KROMOSYNTH_WASM3=0)
    endif()
else()
    target_compile_definitions(KromosynthPlugin PRIVATE KROMOSYNTH_HAS_FAUST=0)
    target_compile_definitions(KromosynthPlugin PRIVATE KROMOSYNTH_WASM3=0)
endif()

# Must come AFTER juce_add_plugin, because it appends POST_BUILD steps that
# have to run after JUCE's own ad-hoc sign and copy. See cmake/Signing.cmake.
kromosynth_sign_plugin_targets(KromosynthPlugin)
