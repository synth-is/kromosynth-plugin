# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Björn Þór Jónsson
# Configure-time assertion that the vendored JUCE is under the open-source
# licence this project claims to comply with.
#
# KEEPABLE.
#
# Why this exists as code rather than a note:
#
# WP_SYNTHIS_PLUGIN.md §0 says JUCE's open-source option is AGPLv3 and asks
# that it be confirmed "before the first public build rather than after". At
# the time this repository was created that claim was UNVERIFIED against disk,
# and the two JUCE copies that were on disk contradicted it:
#
#   kromosynth-evoruns-plugin/native/juce  — JUCE 7.0.5, header says GPL v3
#   vendor/srvb/native/juce                — JUCE 7.0.5, header says GPL v3
#
# Both are JUCE 7, which genuinely was GPLv3, so they do not disprove the
# claim about JUCE 8 — they just mean nobody had checked. This module makes
# the check happen automatically on every fresh configure, and makes the
# answer a build failure rather than a memory.
#
# Note the licence is read from the MODULE HEADER, not from LICENSE.md at the
# repository root and not from juce.com. The module header is what actually
# ships inside the binary's translation units, which is the thing that governs.

function(kromosynth_assert_juce_licence juce_root)
    # juce_core is the module every other module depends on; if anything in
    # the tree is under a given licence, this is.
    set(_header "${juce_root}/modules/juce_core/juce_core.h")

    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR
            "Cannot verify JUCE licence: ${_header} does not exist.")
    endif()

    file(READ "${_header}" _contents)

    # Pull the version out of the BEGIN_JUCE_MODULE_DECLARATION block so the
    # configure log records exactly which JUCE was checked.
    set(_version "unknown")
    if(_contents MATCHES "version:[ \t]*([0-9]+\\.[0-9]+\\.[0-9]+)")
        set(_version "${CMAKE_MATCH_1}")
    endif()

    # Whatever the module declaration claims for `license:`.
    set(_declared "not found")
    if(_contents MATCHES "license:[ \t]*([^\n\r]+)")
        string(STRIP "${CMAKE_MATCH_1}" _declared)
    endif()

    message(STATUS "JUCE ${_version} — module declares license: ${_declared}")

    # The substantive test. AGPL must appear in the header text itself.
    string(FIND "${_contents}" "AGPL" _agpl_at)

    if(_agpl_at EQUAL -1)
        # Report what it DOES say, so the failure is actionable rather than
        # just an assertion that fired.
        set(_says_gpl "no")
        string(FIND "${_contents}" "GPL v3" _gplv3_at)
        if(NOT _gplv3_at EQUAL -1)
            set(_says_gpl "yes — 'GPL v3'")
        endif()

        message(FATAL_ERROR
            "JUCE licence assertion FAILED.\n"
            "  header:            ${_header}\n"
            "  JUCE version:      ${_version}\n"
            "  module declares:   ${_declared}\n"
            "  contains 'AGPL':   no\n"
            "  contains 'GPL v3': ${_says_gpl}\n"
            "\n"
            "WP_SYNTHIS_PLUGIN.md §0 assumes JUCE's open-source option is "
            "AGPLv3, and the open-sourcing argument in that section rests on "
            "it. If this vendored JUCE is GPLv3 instead, that is a FINDING to "
            "record in the WP, not something to work around by deleting this "
            "check.\n"
            "\n"
            "It also bears on the deferred question of libfaust (GPL) under "
            "tier 2 — see WP §9.")
    endif()

    message(STATUS "JUCE licence assertion passed: module header mentions AGPL")
endfunction()
