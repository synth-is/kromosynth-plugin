// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
/*  ProgressSentinel.h — where the site says a render is, carried over the one
    channel that exists.

    HEADER-ONLY, so it needs no CMakeLists change beyond listing the header.

    ── The verb ────────────────────────────────────────────────────────────────

    `synthis://progress?job=<id>&phase=<phase>[&pct=][&title=][&artifact=][&reason=]`
    (docs/RENDER_PROGRESS_DESIGN.md §3). One event per render job, sent by the
    site over the same navigation-sentinel channel auth, checkout and
    acquisition already use.

    ── The greedy-match trap, a fourth time ────────────────────────────────────

    `acquisition::isSentinel` matches ANY `synthis://` URL and parseSentinel
    then answers nullopt for an unknown verb. So a progress URL reaching the
    acquisition branch would be blocked (right) and logged
    `acquisition_sentinel_malformed` (wrong). The progress check must run
    BEFORE the acquisition check in pageAboutToLoad, directly after the auth
    branch — same rule as AuthSentinel.h and CheckoutSentinel.h.

    ── What this deliberately does not carry ──────────────────────────────────

    No ticket, no token, no user id — the whole URL is therefore safe to log,
    unlike acquisition's (see redactForLog). The only free text is `title`,
    a sound name the user is looking at, sanitised below.

    ── landed and lost are NATIVE-ONLY ─────────────────────────────────────────

    The site is never trusted to say a job landed: only AcquisitionRunner
    finishing ok lands an entry. `phase=landed` and `phase=lost` in a sentinel
    therefore parse to nullopt — and the caller must still block the
    navigation, per the same contract as the other verbs.
*/

#pragma once

#include "ArtifactAcquisition.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace kromosynth::progress
{

/** The verb. `synthis://progress?...`, alongside `acquire`, `render`, `auth`
    and `checkout`.
*/
inline constexpr const char* kProgressHost = "progress";

/** Longest job id kept. The job is the takeout entry id, a ULID-ish token;
    anything longer than this is not one, so it is malformed rather than
    clamped — a clamped id could collide with a real one.
*/
inline constexpr int kMaxJob = 64;

/** Longest title kept. A long sound name is a nuisance, not an attack, so it
    is truncated rather than rejected (same rule as auth's kMaxLabel).
*/
inline constexpr int kMaxTitle = 40;

/** The phases. `landed` and `lost` exist in the enum because the ACTIVITY LIST
    uses them (they are native-only states); the PARSER rejects them in a
    sentinel.
*/
enum class Phase { queued, rendering, delivering, failed, landed, lost };

/** Why a job failed. A fixed list — free text from the site is never shown;
    the plugin supplies the wording.
*/
enum class Reason { quota, signed_out, render_failed, timeout, handover_failed, network, other };

/** A parsed progress event. */
struct Event
{
    juce::String job;
    juce::String title;
    juce::String artifact;   // the join key, sent with phase=delivering
    Phase phase = Phase::queued;
    int pct = -1;            // 0..100 with phase=rendering, else -1 (indeterminate)
    Reason reason = Reason::other;
};

/** True for `synthis://progress`, and false for every other sentinel verb.

    Matches the host segment rather than a prefix, so `synthis://progressive`
    does not slip through.
*/
inline bool isProgressSentinel (const juce::String& url)
{
    const juce::String prefix = juce::String (acquisition::kSentinelScheme) + "://";

    if (! url.startsWithIgnoreCase (prefix))
        return false;

    const auto afterScheme = url.fromFirstOccurrenceOf ("://", false, false);
    const auto host = afterScheme.upToFirstOccurrenceOf ("?", false, false)
                                 .upToFirstOccurrenceOf ("/", false, false);

    return host.equalsIgnoreCase (kProgressHost);
}

/** `[A-Za-z0-9_-]{1,64}` — the shape of a takeout entry id. */
inline bool isValidId (const juce::String& s)
{
    if (s.isEmpty() || s.length() > kMaxJob)
        return false;

    for (auto c : s)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                        || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (! ok)
            return false;
    }

    return true;
}

/** Strips anything that could deform the bar or forge a second reading.

    Control characters and newlines out, length clamped — the same rule as
    auth::sanitiseLabel, at the title's own limit.
*/
inline juce::String sanitiseTitle (const juce::String& raw)
{
    juce::String out;
    out.preallocateBytes ((size_t) kMaxTitle * 4);

    for (auto c : raw)
    {
        if (out.length() >= kMaxTitle)
            break;

        if (c >= 32 && c != 127)
            out += c;
    }

    return out.trim();
}

/** The wire value of a phase, or nullopt for a string that is not one.

    `landed` and `lost` DO have wire values here — they are real phases of the
    activity list — so hasUnknownPhase below does not report them; parse
    rejects them separately, as native-only.
*/
inline std::optional<Phase> phaseFromWire (const juce::String& s)
{
    if (s.equalsIgnoreCase ("queued"))     return Phase::queued;
    if (s.equalsIgnoreCase ("rendering"))  return Phase::rendering;
    if (s.equalsIgnoreCase ("delivering")) return Phase::delivering;
    if (s.equalsIgnoreCase ("failed"))     return Phase::failed;
    if (s.equalsIgnoreCase ("landed"))     return Phase::landed;
    if (s.equalsIgnoreCase ("lost"))       return Phase::lost;
    return std::nullopt;
}

/** The wire value, for the log line and the page. Kept next to the parser so
    the two cannot drift into disagreeing about the spelling.
*/
inline juce::String toWire (Phase p)
{
    switch (p)
    {
        case Phase::queued:     return "queued";
        case Phase::rendering:  return "rendering";
        case Phase::delivering: return "delivering";
        case Phase::failed:     return "failed";
        case Phase::landed:     return "landed";
        case Phase::lost:       return "lost";
    }

    return "unknown";
}

/** The wire value of a reason. An unknown or absent value maps to `other`:
    the list is fixed and forward-compatible (fields can be added, never
    repurposed), so a newer site's new code must not nuke the whole event.
*/
inline Reason reasonFromWire (const juce::String& s)
{
    if (s.equalsIgnoreCase ("quota"))           return Reason::quota;
    if (s.equalsIgnoreCase ("signed_out"))      return Reason::signed_out;
    if (s.equalsIgnoreCase ("render_failed"))   return Reason::render_failed;
    if (s.equalsIgnoreCase ("timeout"))         return Reason::timeout;
    if (s.equalsIgnoreCase ("handover_failed")) return Reason::handover_failed;
    if (s.equalsIgnoreCase ("network"))         return Reason::network;
    return Reason::other;
}

inline juce::String toWire (Reason r)
{
    switch (r)
    {
        case Reason::quota:           return "quota";
        case Reason::signed_out:      return "signed_out";
        case Reason::render_failed:   return "render_failed";
        case Reason::timeout:         return "timeout";
        case Reason::handover_failed: return "handover_failed";
        case Reason::network:         return "network";
        case Reason::other:           break;
    }

    return "other";
}

/** True when the URL is a progress sentinel whose `phase` parameter is present
    but not a known wire value. Lets the caller log `progress_sentinel_unknown_phase`
    rather than `progress_sentinel_malformed` — a newer site's new phase is not
    a broken URL.
*/
inline bool hasUnknownPhase (const juce::String& url)
{
    if (! isProgressSentinel (url))
        return false;

    const juce::URL parsed (url);
    const auto idx = parsed.getParameterNames().indexOf ("phase", true);

    if (idx < 0)
        return false;

    return ! phaseFromWire (parsed.getParameterValues()[idx]).has_value();
}

/** Parses a progress sentinel, or nullopt if it is not one or is malformed.

    NULLOPT MUST STILL BE BLOCKED by the caller — the same contract
    parseSentinel and auth::parse document. `phase=landed` and `phase=lost`
    return nullopt here: they are native-only and the site is never trusted
    to say them.
*/
inline std::optional<Event> parse (const juce::String& url)
{
    if (! isProgressSentinel (url))
        return std::nullopt;

    const juce::URL parsed (url);
    const auto names = parsed.getParameterNames();
    const auto values = parsed.getParameterValues();

    const auto param = [&] (const char* name) -> juce::String
    {
        const auto idx = names.indexOf (name, true);
        return idx >= 0 ? values[idx] : juce::String();
    };
    const auto has = [&] (const char* name) { return names.indexOf (name, true) >= 0; };

    Event ev;

    ev.job = param ("job");
    if (! isValidId (ev.job))
        return std::nullopt;

    const auto phase = phaseFromWire (param ("phase"));
    if (! phase.has_value())
        return std::nullopt;   // unknown phase: blocked, logged unknown_phase by the caller
    if (*phase == Phase::landed || *phase == Phase::lost)
        return std::nullopt;   // native-only
    ev.phase = *phase;

    if (has ("pct"))
    {
        const auto raw = param ("pct");

        // An integer or malformed — "abc" must not silently read as 0.
        if (raw.isEmpty() || ! raw.containsOnly ("-0123456789"))
            return std::nullopt;

        // Only meaningful with rendering; ignored with any other phase.
        if (ev.phase == Phase::rendering)
            ev.pct = juce::jlimit (0, 100, raw.getIntValue());
    }

    if (has ("title"))
        ev.title = sanitiseTitle (param ("title"));

    if (has ("artifact"))
    {
        ev.artifact = param ("artifact");

        if (! isValidId (ev.artifact))
            return std::nullopt;
    }

    // The artefact id is the join key between the job (known to the site) and
    // the install (known to native): without it a landing cannot be attached.
    if (ev.phase == Phase::delivering && ev.artifact.isEmpty())
        return std::nullopt;

    if (ev.phase == Phase::failed)
        ev.reason = reasonFromWire (param ("reason"));

    return ev;
}

} // namespace kromosynth::progress
