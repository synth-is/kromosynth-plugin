// InstrumentLibrary — what is on this machine, and how to load one.
//
// Stage 1. Not disposable in the way PluginProcessor is: the shell, the
// acquisition path and any future sync all need an answer to "what do we have
// locally", and this is it.
//
// ── Why a directory scan and not a database ──────────────────────────────────
//
// The artefact layout is already a directory per instrument (STAGE0_FINDINGS.md
// E5): an `instrument.sfz` beside a `samples/` folder. A downloaded artefact
// drops in unchanged. A database would add a second source of truth that can
// disagree with the disk, and the disk is the thing sfizz actually opens.
//
// The cost is that the scan is O(directories) on every refresh. With a few
// hundred instruments that is microseconds; if it ever isn't, cache it then.
//
// ── The layout ───────────────────────────────────────────────────────────────
//
//     ~/Music/Synth.is/Instruments/
//         <artifactId>/
//             instrument.sfz
//             samples/*.wav
//
// ~/Music rather than Application Support because these are the user's
// instruments, not the plugin's private state: they should be visible,
// backup-able and movable without knowing where an app hides things.

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

class InstrumentLibrary
{
public:
    struct Entry
    {
        juce::String id;       // directory name; the artifact id for downloads
        juce::String name;     // display name, from the SFZ header if present
        juce::File sfz;
    };

    /** The root the scan walks. Created on demand, not at construction:
        making a directory as a side effect of asking where it is would be
        surprising.
    */
    static juce::File root();

    /** Rescans. Cheap, and called rather than cached so that an instrument
        dropped in by hand -- or by a download that finished a second ago --
        shows up without a restart.
    */
    std::vector<Entry> scan() const;

    /** Looks up by id. Returns an invalid File if absent, which the caller
        must treat as "not found" rather than "empty path means default".
    */
    juce::File pathFor (const juce::String& id) const;

private:
    /** Reads a display name out of the SFZ comment header if one is there.

        kromosynth-vi writes a block like

            // Kromosynth Virtual Instrument
            // Generated: 2026-08-27T14:27:03.908Z
            // Sounds: 01KPXZJ94GPQEEAZFA6F91YR1W

        None of which is a *name*. So this is best-effort and falls back to the
        directory id, which is at least stable and unique. A real display name
        wants a metadata file the VI service writes -- noted rather than
        invented here, because guessing at a format the generator does not emit
        would create a second convention nobody agreed to.
    */
    static juce::String readDisplayName (const juce::File& sfz, const juce::String& fallbackId);
};
