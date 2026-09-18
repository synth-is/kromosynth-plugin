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
# AUv3 IS enabled now. This paragraph used to say it deliberately was not, and
# the old reasoning is kept rather than deleted because it was sound -- only its
# scope was wrong:
#
#   "An extension cannot read the container app's Documents directory, so
#    InstrumentLibrary and the acquisition session both have to move to an App
#    Group container first. Adding the format before that work exists would
#    produce an AUv3 that loads and then finds an empty instrument library."
#
# Still true, and still exactly what this build does. The correction is that App
# Groups cannot be attached to a wildcard App ID, so whether the entitlement can
# be signed AT ALL is a fact about the Apple developer portal that no amount of
# C++ discovers. juceaide runs at CONFIGURE time (execute_process,
# JUCEUtils.cmake:617), so enabling the format and reading the generated
# .entitlements and Info.plist asks the question for the price of a configure --
# no build, no signing, no device. The empty instrument library is therefore the
# EXPECTED state of this build rather than a regression, and the container work
# is gated on what the artefacts say rather than the other way round.
#
# XCODE GENERATOR ONLY, and permanently. JUCE embeds the appex into the
# standalone app through XCODE_EMBED_APP_EXTENSIONS (JUCEUtils.cmake:1686-1690),
# which is an Xcode-generator property. A Ninja or Makefiles iOS build would
# produce an appex that is never embedded and so never registered with the
# system -- an AUv3 that no host can see, with nothing in the log to say why.
if(KROMOSYNTH_IOS)
    set(_kromosynth_formats Standalone AUv3)
else()
    set(_kromosynth_formats AU VST3)
endif()

# ONE string, two consumers, and they must not be able to disagree.
#
# It goes into the .entitlements that gets signed, and into the C++ that will
# call File::getContainerForSecurityApplicationGroupIdentifier. A mismatch
# between the two produces no error anywhere: that function returns a
# NON-EXISTENT File on failure (juce_File.h says so explicitly),
# InstrumentLibrary::scan() then hits `if (! base.isDirectory()) return
# entries;` and the plugin shows an empty instrument list -- which is
# indistinguishable from a user who has downloaded nothing. Same shape of silent
# failure as the BACKGROUND_AUDIO spelling below, and the reason the string is
# defined once here instead of twice in two languages.
#
# NOT a cache variable, unlike KROMOSYNTH_SFZ_PATH. That one exists to be
# switched between artefacts; this one has to match an identifier registered in
# the Apple developer portal, and a -D override would silently sign an
# entitlement for a group that does not exist. Deliberately not derived from
# BUNDLE_ID either -- Apple requires no relationship between the two, so
# deriving it would assert a coupling nothing enforces.
set(KROMOSYNTH_APP_GROUP_ID "group.is.synth.plugin")

# LSSupportsOpeningDocumentsInPlace, which juce_add_plugin has no option for.
#
# BOTH KEYS ARE NEEDED before an app's Documents folder appears in Files.app.
# UIFileSharingEnabled alone is the older iTunes file-sharing switch; this one
# grants the local file provider access to the same folder, and Apple's own
# guidance is to set the pair. Without it the folder is reachable over a cable
# and invisible on the device, which is a developer's answer and not a user's.
#
# CORRECTION, 2026-09-15. This was first attempted as DOCUMENT_BROWSER_ENABLED
# on the assumption that JUCE's option emitted this key. It does not -- it emits
# UISupportsDocumentBrowser, which declares the app is built around
# UIDocumentBrowserViewController, which this is not. The wrong key was caught
# by reading the BUILT plist, exactly as the trap note below insists.
#
# It was then REVERTED on a second wrong reading: that this key governs whether
# WE can open other apps' documents in place. That is its nominal description;
# its practical effect is the Files.app visibility above. Reverted correctly,
# for the wrong reason, and restored here.
if(KROMOSYNTH_IOS)
    set(KROMOSYNTH_PLIST_MERGE
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<plist version=\"1.0\"><dict>
  <key>LSSupportsOpeningDocumentsInPlace</key><true/>
</dict></plist>")
else()
    set(KROMOSYNTH_PLIST_MERGE "")
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
    PLIST_TO_MERGE           "${KROMOSYNTH_PLIST_MERGE}"
    REQUIRES_FULL_SCREEN     ${KROMOSYNTH_IOS}

    # The shared container the AUv3 and its container app both read the
    # instrument library out of.
    #
    # iOS-only on purpose. On macOS this entitlement would land in the AU and
    # VST3 bundles that cmake/Signing.cmake signs with --options runtime,
    # against a Developer ID profile that does not carry it -- turning a
    # working desktop signature into a failure for an iOS feature.
    #
    # ONE pair covers BOTH iOS targets, which is not obvious and is worth
    # writing down: entitlements are generated per format target but from the
    # SHARED-CODE target's info file (_juce_add_xcode_entitlements,
    # JUCEUtils.cmake:753-770), and juce_Entitlements.cpp's app-groups block is
    # not gated on target kind. So Standalone and AUv3 each get the array
    # without a per-target line.
    #
    # APP_GROUP_IDS is a MULTI-value argument (JUCEUtils.cmake:2144), not a
    # one-value one; docs/NEXT_SESSION_PROMPT_IOS.md §3a claimed both were
    # one-value and that is wrong. It happens not to matter for a single group,
    # because CMake joins lists with ';' and juce_Entitlements.cpp tokenises
    # appGroupIdString on ';' -- so the two spellings coincide here and would
    # diverge the moment a second group is added.
    APP_GROUPS_ENABLED       ${KROMOSYNTH_IOS}
    APP_GROUP_IDS            "${KROMOSYNTH_APP_GROUP_ID}"

    # BRANDING, and it is not only cosmetic on iOS: AudioComponentCopyIcon
    # resolves an AUv3's icon to the CONTAINING APP's icon, so this one keyword
    # supplies both the home-screen icon and our entry in AUM's and
    # GarageBand's plug-in browsers -- which until now was blank while
    # AudioTune and Drambo showed theirs.
    #
    # ONE keyword covers BOTH targets, exactly like the entitlement above:
    # _juce_generate_icon (JUCEUtils.cmake:773) runs per format target but
    # reads the SHARED-CODE target's properties, and on iOS invokes juceaide at
    # CONFIGURE time to build an Images.xcassets containing an AppIcon set.
    #
    # A COMMITTED DERIVED FILE, not a path into the branding folder. Two
    # reasons, and resources/make-appicon.py records both: the branding source
    # lives under one developer's home directory and configure would fail on
    # the hub and the Studio, and the source asset is a DISC with transparent
    # corners while an iOS app icon must be an OPAQUE SQUARE -- alpha is
    # rejected at submission, and iOS applies its own rounded mask, so a disc
    # would have a circle cut out of a circle.
    #
    # juce_add_plugin runs _juce_make_absolute_and_check on this, so a missing
    # or moved file fails at configure rather than silently producing no icon.
    ICON_BIG "${CMAKE_CURRENT_LIST_DIR}/../resources/AppIcon-1024.png"

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
    RenderOutbox.cpp
    RenderOutbox.h
    RenderList.cpp
    RenderList.h
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

    # Defined on EVERY platform, not only iOS, for the same reason
    # KROMOSYNTH_HAS_SAMPLER is defined as 1-or-0 rather than defined-or-absent:
    # an #ifdef on a name that is only sometimes there is a bad place to rely on
    # silence. It is the ENTITLEMENT above and not this string that makes the
    # container resolvable, so on macOS this is a constant nothing looks up.
    KROMOSYNTH_APP_GROUP_ID="${KROMOSYNTH_APP_GROUP_ID}"
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
