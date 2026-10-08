// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
/*  AuthSentinel.h — who the site says you are, carried over the one channel
    that exists.

    HEADER-ONLY, so it needs no CMakeLists change.

    ── Why a sentinel and not the bridge ──────────────────────────────────────

    The strip is in the shell's document on `juce://juce.backend`; the site is
    on `https://synth.is`. Different origins, no shared storage, no
    postMessage, and the site's WebBrowserComponent has native integration
    deliberately OFF so that an off-origin sign-in redirect cannot inherit
    `window.__JUCE__` (PluginEditor.h, InstrumentedBrowser::pageAboutToLoad).

    So the shell CANNOT read the site's session. What already exists is the
    navigation sentinel: the site sets `window.location.href` to a `synthis://`
    URL, the plugin catches it in pageAboutToLoad and blocks it. Acquisition has
    used that since Stage 1. Auth rides the same rails rather than opening a
    second hole.

    ── The greedy-match trap ──────────────────────────────────────────────────

    `acquisition::isSentinel` matches ANY `synthis://` URL, and parseSentinel
    then returns nullopt for an unknown verb. So before this header existed, a
    `synthis://auth` URL was caught by the acquisition branch, blocked (right)
    and logged as `acquisition_sentinel_malformed` (wrong, and misleading to
    anyone reading the log). The auth check must therefore run BEFORE the
    acquisition check in pageAboutToLoad, not after.

    ── What this deliberately does not carry ──────────────────────────────────

    No token, no user id, no email. The plugin does not act on the user's
    behalf and has no use for a credential; carrying one would put it in
    ~/Library/Logs the first time somebody logged a URL without redacting it,
    which is the mistake acquisition's own comment records having nearly made.
    A display label and three enum values are the whole payload.
*/

#pragma once

#include "ArtifactAcquisition.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace kromosynth::auth
{

/** The verb. `synthis://auth?...`, alongside acquisition's `synthis://acquire`. */
inline constexpr const char* kAuthHost = "auth";

/** Longest display label kept. Anything longer is truncated rather than
    rejected -- a 300-character name is a nuisance, not an attack, and dropping
    the whole sentinel over it would leave the bar showing nothing.
*/
inline constexpr int kMaxLabel = 24;

/** What the site last said about who you are.

    THREE STATES, NOT TWO, and the third is the point. An anonymous identity is
    not "signed in" and not "signed out": the web app creates one silently on
    sign-out, likes attach to it, and it looked identical to a real account in
    the plugin until this existed. Collapsing it into either neighbour
    reproduces the bug this was written for.
*/
struct State
{
    enum class Kind { unknown, none, anonymous, registered };

    Kind kind = Kind::unknown;

    /** Short display label -- an initial, a handle. Never a token. May be empty
        even when kind is registered; the caller must cope.
    */
    juce::String label;

    /** Whether anything is unread. A BOOL, NOT A COUNT, deliberately: the count
        changes server-side with no navigation to hang a sentinel on, so a
        number in the bar would be confidently wrong most of the time. A dot
        goes stale honestly.
    */
    bool unread = false;

    bool isKnown() const noexcept { return kind != Kind::unknown; }
};

/** True for `synthis://auth`, and false for every other sentinel verb.

    Matches the host segment rather than a prefix, so `synthis://authorise`
    does not slip through.
*/
inline bool isAuthSentinel (const juce::String& url)
{
    const juce::String prefix = juce::String (acquisition::kSentinelScheme) + "://";

    if (! url.startsWithIgnoreCase (prefix))
        return false;

    const auto afterScheme = url.fromFirstOccurrenceOf ("://", false, false);
    const auto host = afterScheme.upToFirstOccurrenceOf ("?", false, false)
                                 .upToFirstOccurrenceOf ("/", false, false);

    return host.equalsIgnoreCase (kAuthHost);
}

/** Strips anything that could deform the bar or forge a second reading.

    Control characters and newlines out, length clamped. The label is written
    into the page as textContent rather than HTML, so this is about layout and
    log legibility rather than injection -- but a label containing a newline
    would still make one log line look like two.
*/
inline juce::String sanitiseLabel (const juce::String& raw)
{
    juce::String out;
    out.preallocateBytes ((size_t) kMaxLabel * 4);

    for (auto c : raw)
    {
        if (out.length() >= kMaxLabel)
            break;

        if (c >= 32 && c != 127)
            out += c;
    }

    return out.trim();
}

/** Parses an auth sentinel, or nullopt if it is not one or is malformed.

    NULLOPT MUST STILL BE BLOCKED by the caller. Returning nullopt says "I
    could not read this", not "let it navigate" -- the same contract
    parseSentinel documents, and for the same reason.
*/
inline std::optional<State> parse (const juce::String& url)
{
    if (! isAuthSentinel (url))
        return std::nullopt;

    const juce::URL parsed (url);

    const auto rawKind = parsed.getParameterValues()[
        parsed.getParameterNames().indexOf ("state", true)];

    State s;

    if (rawKind.equalsIgnoreCase ("registered"))      s.kind = State::Kind::registered;
    else if (rawKind.equalsIgnoreCase ("anonymous"))  s.kind = State::Kind::anonymous;
    else if (rawKind.equalsIgnoreCase ("none"))       s.kind = State::Kind::none;
    else
        return std::nullopt;   // unknown or absent: refuse rather than guess

    const auto labelIdx = parsed.getParameterNames().indexOf ("label", true);
    if (labelIdx >= 0)
        s.label = sanitiseLabel (parsed.getParameterValues()[labelIdx]);

    const auto unreadIdx = parsed.getParameterNames().indexOf ("unread", true);
    if (unreadIdx >= 0)
        s.unread = parsed.getParameterValues()[unreadIdx].getIntValue() > 0;

    return s;
}

/** The wire value, for the page. Kept next to the parser so the two cannot
    drift into disagreeing about the spelling.
*/
inline juce::String toWire (State::Kind k)
{
    switch (k)
    {
        case State::Kind::registered: return "registered";
        case State::Kind::anonymous:  return "anonymous";
        case State::Kind::none:       return "none";
        case State::Kind::unknown:    break;
    }

    return "unknown";
}

} // namespace kromosynth::auth
