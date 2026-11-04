// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
/*  ActivityList.h — the render-activity list the editor keeps and the shell
    page pulls (docs/RENDER_PROGRESS_DESIGN.md §2, §4, §6 Q5).

    HEADER-ONLY, and the clock is INJECTED, so tools/acquire-probe can exercise
    every transition rule with no window, no webview and no real time.

    ── One entry per job ───────────────────────────────────────────────────────

    The job is the takeout entry id the site already has for every render. An
    entry carries the sound title, the phase and the percent, and finishes as
    landed or failed. `landed` is set ONLY by AcquisitionRunner finishing ok
    (joined on the artefact id the `delivering` sentinel carried); `lost` is
    the lease running out. Terminal entries expire after 8 s so the capsule
    can show them and then let them go.

    ── The lease ───────────────────────────────────────────────────────────────

    A hidden or occluded site document ticks about once a minute (measured,
    design §5.2), so the site's 15 s heartbeat can arrive a minute apart. The
    lease is therefore sized for that: after 60 s without an event the ring
    drops to indeterminate (pct reported as -1), after 180 s the entry is
    lost, and NO silence clock runs while the site view is hidden — it
    restarts from zero when the site is shown again. A full load of the site
    view (pageFinishedLoading) loses every non-terminal entry at once, and a
    25 min hard cap applies regardless of heartbeats.
*/

#pragma once

#include "ProgressSentinel.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace kromosynth::activity
{

/** Silence after which the ring drops to indeterminate (pct reported as -1). */
inline constexpr int64_t kIndeterminateMs = 60 * 1000;

/** Silence after which a non-terminal entry becomes lost. */
inline constexpr int64_t kLostMs = 180 * 1000;

/** Hard cap from the first event, heartbeats notwithstanding. */
inline constexpr int64_t kHardCapMs = 25 * 60 * 1000;

/** How long a terminal entry (landed/failed/lost) stays for the capsule. */
inline constexpr int64_t kTerminalExpiryMs = 8 * 1000;

/** Forward-only rank: queued 1, rendering 2, delivering 3, landed 4.
    failed/lost are unranked — reachable from any non-terminal state.
*/
inline int rank (progress::Phase p)
{
    switch (p)
    {
        case progress::Phase::queued:     return 1;
        case progress::Phase::rendering:  return 2;
        case progress::Phase::delivering: return 3;
        case progress::Phase::landed:     return 4;
        case progress::Phase::failed:
        case progress::Phase::lost:       break;
    }

    return 0;
}

inline bool isTerminal (progress::Phase p)
{
    return p == progress::Phase::landed || p == progress::Phase::failed
        || p == progress::Phase::lost;
}

struct Entry
{
    juce::String job;
    juce::String title;
    progress::Phase phase = progress::Phase::queued;
    int pct = -1;
    juce::String artifact;   // join key, set by the delivering sentinel
    juce::String reason;     // wire code, set on failed
    bool instrument = true;  // landed via the acquire verb (counts toward the badge)
    bool unseen = false;     // landed and not yet seen in the Library
    int64_t startedMs = 0;
    int64_t updatedMs = 0;
    int64_t silenceBaseMs = 0;   // last event, or the last time the site was shown
};

class List
{
public:
    using Clock = std::function<int64_t ()>;

    explicit List (Clock clock) : clock_ (std::move (clock)) {}

    /** Applies one sentinel event. Returns true when the list changed, so the
        caller knows to push.
    */
    bool apply (const progress::Event& ev)
    {
        const auto now = clock_();
        Entry* e = find (ev.job);

        if (e == nullptr)
        {
            Entry fresh;
            fresh.job = ev.job;
            fresh.title = ev.title;
            fresh.phase = ev.phase;
            fresh.pct = ev.pct;
            fresh.artifact = ev.artifact;
            fresh.reason = ev.phase == progress::Phase::failed ? progress::toWire (ev.reason)
                                                               : juce::String();
            fresh.startedMs = fresh.updatedMs = fresh.silenceBaseMs = now;
            entries_.push_back (fresh);
            return true;
        }

        // Terminal entries take no further sentinel events (a landing revives
        // a LOST one — that is land() below, not this).
        if (isTerminal (e->phase))
            return false;

        if (ev.phase == progress::Phase::failed)
        {
            e->phase = progress::Phase::failed;
            e->reason = progress::toWire (ev.reason);
            e->pct = -1;
            e->updatedMs = e->silenceBaseMs = now;
            return true;
        }

        // Forward only: a lower rank than the entry's current phase is ignored.
        if (rank (ev.phase) < rank (e->phase))
            return false;

        e->phase = ev.phase;
        e->pct = ev.pct;

        if (ev.artifact.isNotEmpty())
            e->artifact = ev.artifact;

        // The first event for a job carries the title; later ones may omit it.
        if (e->title.isEmpty() && ev.title.isNotEmpty())
            e->title = ev.title;

        e->updatedMs = e->silenceBaseMs = now;
        return true;
    }

    /** The landed push: AcquisitionRunner finished ok. Joins on the artefact
        id; an acquisition with no known job still creates a landed entry,
        titled from the request's name. Revives a lost entry. Returns true
        when the list changed.
    */
    bool land (const juce::String& artifactId, const juce::String& title, bool instrument)
    {
        const auto now = clock_();
        Entry* e = findByArtifact (artifactId);

        if (e == nullptr)
        {
            Entry fresh;
            fresh.job = artifactId;   // no known job: the artefact id is the key
            fresh.title = title;
            fresh.artifact = artifactId;
            fresh.phase = progress::Phase::landed;
            fresh.instrument = instrument;
            fresh.unseen = true;
            fresh.startedMs = fresh.updatedMs = fresh.silenceBaseMs = now;
            entries_.push_back (fresh);
            return true;
        }

        // landed stays landed; failed is final — only lost revives.
        if (e->phase == progress::Phase::landed || e->phase == progress::Phase::failed)
            return false;

        e->phase = progress::Phase::landed;
        e->artifact = artifactId;

        if (e->title.isEmpty())
            e->title = title;

        e->instrument = instrument;
        e->unseen = true;
        e->pct = -1;
        e->updatedMs = e->silenceBaseMs = now;
        return true;
    }

    /** A failed acquisition (the runner finished not-ok) marks the matching
        non-terminal entry failed. With no known job there is nothing to mark:
        the shell's own poll already shows the failure message.
    */
    bool fail (const juce::String& artifactId, const juce::String& reason)
    {
        const auto now = clock_();
        Entry* e = findByArtifact (artifactId);

        if (e == nullptr || isTerminal (e->phase))
            return false;

        e->phase = progress::Phase::failed;
        e->reason = reason;
        e->pct = -1;
        e->updatedMs = e->silenceBaseMs = now;
        return true;
    }

    /** The site view was hidden or shown. While it is hidden no silence clock
        runs; showing it again restarts every non-terminal entry's clock from
        zero.
    */
    void setSiteHidden (bool hidden)
    {
        if (hidden == siteHidden_)
            return;

        siteHidden_ = hidden;

        if (! hidden)
        {
            const auto now = clock_();

            for (auto& e : entries_)
                if (! isTerminal (e.phase))
                    e.silenceBaseMs = now;
        }
    }

    /** A full load of the site view: the document was replaced and every
        follower on it died, so every non-terminal entry is lost at once.
    */
    bool siteReloaded()
    {
        const auto now = clock_();
        bool changed = false;

        for (auto& e : entries_)
        {
            if (isTerminal (e.phase))
                continue;

            e.phase = progress::Phase::lost;
            e.pct = -1;
            e.updatedMs = e.silenceBaseMs = now;
            changed = true;
        }

        return changed;
    }

    /** The lease, run from the editor's timerCallback. Returns true when the
        list changed.
    */
    bool tick()
    {
        const auto now = clock_();
        bool changed = false;

        for (auto it = entries_.begin(); it != entries_.end();)
        {
            Entry& e = *it;

            if (isTerminal (e.phase))
            {
                if (now - e.updatedMs >= kTerminalExpiryMs)
                {
                    it = entries_.erase (it);
                    changed = true;
                }
                else
                {
                    ++it;
                }

                continue;
            }

            // The hard cap runs from the first event and applies while the
            // site view is hidden too.
            if (now - e.startedMs >= kHardCapMs)
            {
                e.phase = progress::Phase::lost;
                e.pct = -1;
                e.updatedMs = e.silenceBaseMs = now;
                changed = true;
                ++it;
                continue;
            }

            if (! siteHidden_)
            {
                const auto silence = now - e.silenceBaseMs;

                if (silence >= kLostMs)
                {
                    e.phase = progress::Phase::lost;
                    e.pct = -1;
                    e.updatedMs = e.silenceBaseMs = now;
                    changed = true;
                }
                else if (silence >= kIndeterminateMs && e.pct != -1)
                {
                    e.pct = -1;   // the ring drops to indeterminate
                    changed = true;
                }
            }

            ++it;
        }

        return changed;
    }

    /** Opening the Library counts as having seen the landings. */
    bool clearUnseen()
    {
        bool changed = false;

        for (auto& e : entries_)
            if (e.unseen)
            {
                e.unseen = false;
                changed = true;
            }

        return changed;
    }

    /** The getActivity JSON (design §4): now, active, unseenLanded, entries. */
    juce::var toJson() const
    {
        const auto now = clock_();

        auto* root = new juce::DynamicObject();
        root->setProperty ("now", juce::var (now));

        int active = 0;
        int unseenLanded = 0;
        juce::Array<juce::var> entries;

        for (const auto& e : entries_)
        {
            if (! isTerminal (e.phase))
                ++active;

            if (e.phase == progress::Phase::landed && e.unseen && e.instrument)
                ++unseenLanded;

            auto* o = new juce::DynamicObject();
            o->setProperty ("job", e.job);
            o->setProperty ("title", e.title);
            o->setProperty ("phase", progress::toWire (e.phase));
            o->setProperty ("pct", e.pct);
            o->setProperty ("startedMs", juce::var (e.startedMs));
            o->setProperty ("updatedMs", juce::var (e.updatedMs));

            if (e.artifact.isNotEmpty())
                o->setProperty ("artifact", e.artifact);

            if (e.reason.isNotEmpty())
                o->setProperty ("reason", e.reason);

            entries.add (juce::var (o));
        }

        root->setProperty ("active", active);
        root->setProperty ("unseenLanded", unseenLanded);
        root->setProperty ("entries", juce::var (entries));

        return juce::var (root);
    }

private:
    Entry* find (const juce::String& job)
    {
        for (auto& e : entries_)
            if (e.job == job)
                return &e;

        return nullptr;
    }

    Entry* findByArtifact (const juce::String& artifactId)
    {
        for (auto& e : entries_)
            if (e.artifact == artifactId && artifactId.isNotEmpty())
                return &e;

        return nullptr;
    }

    Clock clock_;
    std::vector<Entry> entries_;

    /** The site view starts hidden behind the shell, and no silence clock
        runs in that state.
    */
    bool siteHidden_ = true;
};

} // namespace kromosynth::activity
