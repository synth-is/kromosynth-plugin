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
    enum class Kind { none, sampler, faust };

    /** The seven perceptual dimensions, in the index order of
        DescriptorLabelingService's DESCRIPTOR_DIMENSIONS. Fixed here rather
        than read from the sidecar so that a file which omits or reorders them
        cannot silently shift what a column means.
    */
    static constexpr int kNumDescriptors = 7;
    static const char* descriptorName (int index);

    /** Everything synthis.json says about an instrument.

        EVERY FIELD IS OPTIONAL and the defaults mean "absent", never zero.
        Three populations legitimately carry almost none of this: instruments
        that predate the contract, instruments placed here by hand, and renders
        of unadopted discovery-feed candidates, which have no `sounds` node and
        therefore no descriptors, sound type or adoption date at all
        (ARTEFACT_CONTRACT §5). Treating a missing field as a measured zero
        would make those three indistinguishable from a dark, quiet instrument
        discovered at the epoch.
    */
    struct Metadata
    {
        juce::String name;

        // ── source ──────────────────────────────────────────────────────────
        juce::String soundId, substrate, evolutionRunId, soundClass, creator;
        juce::String discoveredAt, adoptedAt, renderedAt, acquiredAt;  // ISO 8601
        int generation = -1;

        // ── character ───────────────────────────────────────────────────────
        juce::String soundType;
        juce::String labels[kNumDescriptors];
        float descriptors[kNumDescriptors] {};
        bool hasDescriptors = false;

        /** The labels are P33/P67 against the whole corpus on the day they
            were assigned, so they are a claim about the archive as much as
            about the sound, and DescriptorLabelingService::relabelAll moves
            them. Carried so a reader knows what it is holding; the plugin
            ranks on `descriptors` against its own library instead, which needs
            no corpus and cannot drift. ARTEFACT_CONTRACT §3.3.
        */
        bool labelsAreCorpusRelative = false;
        juce::String labelledAt;

        // ── measured ────────────────────────────────────────────────────────
        bool hasMeasured = false;
        float trim = 0.0f, trimDb = 0.0f, peak = 0.0f;
        double rtf = 0.0;
        juce::String rtfPlatform, articulation;
        int capAt4x = 0;
        bool hasGate = false;

        bool isEmpty() const noexcept { return name.isEmpty() && ! hasMeasured
                                            && ! hasDescriptors && soundType.isEmpty(); }
    };

    struct Entry
    {
        juce::String id;       // directory name; the artifact id for downloads
        juce::String name;     // display name, from the SFZ header if present
        juce::File primary;    // instrument.sfz, or instrument.wasm
        Kind kind = Kind::sampler;
        Metadata meta;         // everything the sidecar said; mostly empty today
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

    /** What kind of instrument an id names, and the files it needs.

        An installed instrument is a folder, and what is inside decides which
        engine plays it: `instrument.sfz` is a rendered sample set for sfizz,
        `instrument.wasm` plus `instrument.json` is a live Faust program. The
        site does not have to say which; the artefact already knows.

        The .wasm/.json pair is PROVISIONAL. Nothing produces one yet -- the
        takeout path still ships a ZIP of samples -- so these names are chosen
        here and will have to be agreed with kromosynth-vi before anything
        depends on them.
    */
    struct Resolved
    {
        Kind kind = Kind::none;
        juce::File primary;    ///< instrument.sfz, or instrument.wasm
        juce::File metadata;   ///< empty for sampler; instrument.json for Faust
    };

    Resolved resolve (const juce::String& id) const;

    /** Reads the display name from an instrument's synthis.json, written at
        acquisition time from the label the site showed.

        Empty when the file is absent or malformed, so callers fall back to the
        SFZ header. Public because it is a stateless reader of a documented file
        format and because scan() walks a fixed root, which leaves no other way
        to exercise it against a fixture.
    */
    static juce::String readSidecarName (const juce::File& instrumentFolder);

    /** The whole sidecar, parsed. Returns an empty Metadata when the file is
        absent or malformed -- ARTEFACT_CONTRACT §3 makes every field optional,
        so "absent" and "unparseable" are the same outcome for a reader and
        neither costs the instrument its place in the list.

        readSidecarName delegates here rather than parsing separately, so the
        name the list shows and the metadata it sorts by cannot come from two
        different readings of the same file.
    */
    static Metadata readSidecar (const juce::File& instrumentFolder);

private:
    /** Reads a display name out of the SFZ comment header if one is there.

        kromosynth-vi writes a block like

            // Kromosynth Virtual Instrument
            // Generated: 2026-08-27T14:27:03.908Z
            // Sounds: 01KPXZJ94GPQEEAZFA6F91YR1W

        and the XY generator writes "Sound:" singular under an "XY Morph"
        title. None of which is a *name*: a ULID is an identifier, and so is
        the directory it falls back to.

        KNOWN GAP, not a bug to fix here. A usable display name needs the VI
        service to emit one, and inventing a format the generator does not
        write would create a second convention nobody agreed to. The header
        does carry genuinely descriptive material -- "Range: C3 - C6",
        "Velocity Layers: 3", "XY Morph" -- which would make a better label
        than either identifier. Proposed rather than implemented: it is a UI
        convention, and those should be decided rather than accreted.
    */
    static juce::String readDisplayName (const juce::File& sfz, const juce::String& fallbackId);
};
