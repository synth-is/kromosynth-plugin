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
#include <functional>
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

    /** The value CLAP reports for `id` now, for probes. */
    bool paramValue (juce::uint32 id, double& out);

    // ── the engine's own page (synthis-et4.4) ───────────────────────────────────────────────────────────
    // A WCLAP built from the open-webclap kit carries its editor as a web page (the bundle's ui/) and
    // talks to the DSP side through clap.webview: binary frames in both directions, protocol in
    // open-webclap's juce_webclap_protocol.h. This engine is the DSP side's host: it opens the guest's gui
    // in the "webview" API (no native window; the bridge's own cocoa webview is NOT used, see
    // docs/WCLAP_ENGINE_UI_2026-10-27.md for why), and relays frames. The page itself is WclapEngineView's.
    //
    // Message thread only, every call. `toPage` may be called from ANY thread (the guest also sends from
    // inside process()); it must only queue.
    using PageSink = std::function<void (const void* data, size_t size)>;
    struct GuiOpenResult
    {
        bool ok = false;
        juce::String detail;
        juce::String uri;                    ///< start page, e.g. "/ui/index.html"
        juce::uint32 width = 0, height = 0;  ///< the guest's idea of the editor size, logical pixels
    };
    GuiOpenResult guiOpen (PageSink toPage);
    void guiClose();
    bool guiIsOpen() const noexcept { return guiOpen_.load(); }

    /** page -> engine. One protocol frame, exactly the bytes the page posted. */
    bool guiReceive (const void* data, size_t size);

    /** The guest asked for a main-thread callback (request_callback); runs it. Cheap when nothing is due. */
    void guiService();

    /** Bumped each time a guest instance is created (load, reactivate): a view built for an older one is stale. */
    int generation() const noexcept { return generation_.load(); }
    juce::File bundleDir() const;

    /** Per-call time limit handed to wclap_global_init, ms. Surge XT's init needs far more than the
        bridge's 2000 ms default. 0 = no limit (and no epoch thread). Overridable for measurement with
        KROMOSYNTH_WCLAP_TIME_LIMIT_MS. */
    static unsigned timeLimitMs();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> loaded_ { false };
    std::atomic<bool> guiOpen_ { false };
    std::atomic<int> generation_ { 0 };
    std::atomic<float> lastPeak_ { 0.0f };
    std::atomic<juce::int64> blocks_ { 0 };
    juce::String name_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WclapEngine)
};
