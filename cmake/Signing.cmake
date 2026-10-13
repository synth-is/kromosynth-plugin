# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# Code signing — Developer ID, hardened runtime, notarisation.
#
# KEEPABLE.
#
# ── Why this is not a Stage 1 concern ────────────────────────────────────────
#
# STAGE0_FINDINGS.md A2: GarageBand refuses an ad-hoc signed plugin until the
# user accepts a system dialog reading "Use of the requested Audio Unit(s)
# requires lowering the security settings". WP section 7 schedules signing for
# Stage 1, i.e. before distribution. For Apple hosts it is LOAD-BLOCKING, and no
# real user can ever be shown that dialog.
#
# ── The target configuration, measured rather than assumed ───────────────────
#
# A2 asked for its own inference to be re-verified against a genuinely signed
# build. Read off the two commercial samplers installed on the dev machine:
#
#   plugin         signature                 hardened runtime  entitlements  spctl
#   sforzando      Developer ID (Plogue)     no                none          accepted
#   DecentSampler  Developer ID (Decidedly)  yes               none          accepted
#   ours (ad-hoc)  ad-hoc                    no                none          REJECTED
#
# Three things follow, and the second is the one that saves work:
#
#   1. `spctl` rejects ours and accepts both of theirs. That is the assessment
#      behind the dialog, so A2's inference is confirmed.
#   2. NEITHER commercial plugin ships any entitlements. The hardened runtime
#      needs no exceptions for a plugin of this shape -- including the webview,
#      because WKWebView's JIT lives in a separate WebContent process and is not
#      ours to declare. So there is no entitlements plist here, deliberately.
#      If one ever becomes necessary, that is a finding worth recording, not a
#      file to add quietly.
#   3. sforzando is notarised WITHOUT the hardened runtime; DecentSampler has
#      it. Notarisation has required hardened runtime for new submissions since
#      2019, so sforzando is a legacy allowance and DecentSampler is the model.
#
# ── Ordering, which is the part that is easy to get wrong ────────────────────
#
# juce_add_plugin already attaches two POST_BUILD steps to each format target,
# in this order (JUCEUtils.cmake:1044, :1055):
#
#   1. _juce_adhoc_sign  -- the "Replacing invalid signature with ad-hoc
#                           signature" line in the build log
#   2. _juce_copy_dir    -- copy the bundle to ~/Library/Audio/Plug-Ins/...
#
# Anything added here runs after BOTH. So signing only the build artefact would
# leave the INSTALLED copy carrying JUCE's ad-hoc signature, and the installed
# copy is the one every host actually loads. That failure would look exactly
# like signing not working at all.
#
# This therefore re-signs the artefact and then re-copies it, reusing JUCE's own
# copyDir.cmake so the two copies cannot drift.

set(KROMOSYNTH_SIGN_IDENTITY "" CACHE STRING
    "Codesign identity, e.g. 'Developer ID Application: Name (TEAMID)'. \
Empty leaves JUCE's ad-hoc signature in place, which is fine for non-Apple \
hosts and is refused by GarageBand and Logic. Run \
`security find-identity -v -p codesigning` to list.")

option(KROMOSYNTH_HARDENED_RUNTIME
       "Sign with the hardened runtime. Required for notarisation." ON)

function(kromosynth_sign_plugin_targets shared_code_target)
    if(NOT APPLE)
        return()
    endif()

    if(NOT KROMOSYNTH_SIGN_IDENTITY)
        message(STATUS
            "Signing: ad-hoc (KROMOSYNTH_SIGN_IDENTITY empty). "
            "Apple hosts will demand a security exception -- see A2.")
        return()
    endif()

    set(codesign_flags --force --timestamp)

    if(KROMOSYNTH_HARDENED_RUNTIME)
        list(APPEND codesign_flags --options runtime)
    endif()

    # Deliberately NOT --deep. Apple documents it as deprecated and it signs
    # nested code with the SAME flags as the outer bundle, which is wrong the
    # moment anything nested needs different treatment. These bundles have no
    # nested code today -- sfizz and JUCE are both static -- so a single
    # non-recursive signature is both correct and honest about that assumption.

    get_target_property(active_targets
                        "${shared_code_target}" JUCE_ACTIVE_PLUGIN_TARGETS)

    foreach(fmt_target IN LISTS active_targets)
        get_target_property(kind "${fmt_target}" JUCE_TARGET_KIND_STRING)

        if(kind STREQUAL "App")
            continue()
        endif()

        get_target_property(artefact "${fmt_target}" JUCE_PLUGIN_ARTEFACT_FILE)

        if(NOT artefact)
            continue()
        endif()

        add_custom_command(TARGET "${fmt_target}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E echo
                    "Signing ${kind}: ${KROMOSYNTH_SIGN_IDENTITY}"
            COMMAND codesign ${codesign_flags}
                    --sign "${KROMOSYNTH_SIGN_IDENTITY}"
                    "$<GENEX_EVAL:${artefact}>"
            # Verify immediately. A signature that codesign accepts but
            # `--verify --deep --strict` rejects is a thing that happens, and
            # discovering it at notarisation instead of at build time costs an
            # upload round trip.
            COMMAND codesign --verify --deep --strict --verbose=2
                    "$<GENEX_EVAL:${artefact}>"
            VERBATIM)

        # Re-copy over JUCE's already-copied, ad-hoc-signed bundle.
        get_target_property(dest "${fmt_target}" JUCE_PLUGIN_COPY_DIR)

        # JUCE sets JUCE_PLUGIN_COPY_DIR whether or not the copy step is
        # enabled, so `dest` alone is not evidence that a copy is wanted.
        # Without this guard the re-copy installed into ~/Library/Audio/Plug-Ins
        # even with KROMOSYNTH_COPY_AFTER_BUILD=OFF (scripts/package.sh).
        if(dest AND KROMOSYNTH_COPY_AFTER_BUILD)
            add_custom_command(TARGET "${fmt_target}" POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                        "-Dsrc=$<GENEX_EVAL:${artefact}>"
                        "-Ddest=$<GENEX_EVAL:${dest}>"
                        "-P" "${JUCE_CMAKE_UTILS_DIR}/copyDir.cmake"
                VERBATIM)
        endif()
    endforeach()

    message(STATUS "Signing: ${KROMOSYNTH_SIGN_IDENTITY}"
                   " (hardened runtime: ${KROMOSYNTH_HARDENED_RUNTIME})")
endfunction()
