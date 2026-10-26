// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// WclapEngine — hosts ONE WCLAP (a CLAP plugin compiled to WebAssembly, e.g. the Surge XT bundle)
// inside the plugin, through the wclap-bridge on the same Wasmtime FaustEngine uses.
//
// synthis-et4.3 SPIKE. Compiled only with -DSYNTHIS_WCLAP_HOST=ON (cmake/WclapBridge.cmake); the
// shipping build never sees this file. DISPOSABLE in the way PluginProcessor is: when a second hosted
// engine exists, the shape of "load / process / state" belongs in a common interface, not here.
//
// What it does, in the order a CLAP host must do it:
//   open bundle -> factory -> create_plugin -> init -> activate -> start_processing -> ONE EMPTY BLOCK
// and on loadState: state->load -> one empty block again. The empty block is the Six Sines finding
// (docs/OPEN_WEBCLAP_SIX_SINES_2026-10-20.md): the first note after activate/loadState is wrong
// otherwise. It is applied to every WCLAP because it is cheap and not harmful.
//
// Threads. load/saveState/loadState/unload belong to the message thread with the processor suspended;
// process() belongs to the audio thread. The bridge serialises every guest call behind one recursive
// mutex (a single-thread module has one Wasmtime store), and process() additionally TRY-locks this
// engine's own lock and renders silence rather than wait behind a state save.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

class WclapEngine
{
public:
    WclapEngine();
    ~WclapEngine();

    struct LoadResult
    {
        bool ok = false;
        juce::String detail;       ///< why not, or the plugin id when ok
        juce::String pluginId, pluginName;
        double loadMs = 0.0;       ///< open + create + activate + settle
    };

    /** @param wclapDir  the `instrument.wclap` directory (holds module.wasm)
        @param dataDir   writable; presets/, cache/ and var/ are created under it (Surge XT wants a
                         writable /user: docs/PLUGIN_AS_HOST_SPIKE.md finding 3) */
    LoadResult load (const juce::File& wclapDir, const juce::File& dataDir,
                     double sampleRate, int maxBlock);

    /** Same bundle at a new rate / larger block: the plugin's state is carried to a fresh instance. */
    bool reactivate (double sampleRate, int maxBlock);

    void unload();
    bool isLoaded() const noexcept { return loaded_.load(); }

    /** Adds the plugin's audio to `buffer` after clearing it; MIDI becomes CLAP note / midi events. */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    /** clap.state. loadState is followed by the empty settle block. */
    bool saveState (juce::MemoryBlock& dest);
    bool loadState (const void* data, size_t size);

    /** The guest's CLAP parameters, for probes and (later) host automation. */
    struct ParamInfo
    {
        juce::uint32 id = 0;
        juce::String name;
        double minValue = 0, maxValue = 1, defaultValue = 0;
        bool hidden = false, readOnly = false, stepped = false;
    };
    int numParams();
    bool paramInfo (int index, ParamInfo& out);

    /** Delivered as a CLAP_EVENT_PARAM_VALUE at the start of the next process(). */
    void queueParam (juce::uint32 id, double value);

    /** Peak |sample| of the last process() and the number of blocks rendered; for probes and the log. */
    float lastPeak() const noexcept { return lastPeak_.load (std::memory_order_relaxed); }
    juce::int64 blocksRendered() const noexcept { return blocks_.load (std::memory_order_relaxed); }
    juce::String pluginName() const { return name_; }

    /** Per-call time limit handed to wclap_global_init, ms. Surge XT's init needs far more than the
        bridge's 2000 ms default. 0 = no limit (and no epoch thread). Overridable for measurement with
        KROMOSYNTH_WCLAP_TIME_LIMIT_MS. */
    static unsigned timeLimitMs();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> loaded_ { false };
    std::atomic<float> lastPeak_ { 0.0f };
    std::atomic<juce::int64> blocks_ { 0 };
    juce::String name_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WclapEngine)
};
