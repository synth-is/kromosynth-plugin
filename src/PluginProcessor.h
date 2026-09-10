// ─────────────────────────────────────────────────────────────────────────────
// DISPOSABLE. Rewrite or delete without ceremony.
//
// Stage 0 processor, now with audio. No parameters, no state, no UI bridge.
//
// Everything here that looks like a decision is still a placeholder:
//   • getStateInformation writes nothing. State is Stage 1.
//   • There is no AudioProcessorValueTreeState, on purpose. Adding one now
//     would be an abstraction over zero parameters.
//   • The SFZ path is compiled in. Changing instrument is a reconfigure, not
//     a code edit and not a UI action — see KROMOSYNTH_SFZ_PATH.
//
// Do not build on this file. Build beside it and delete it.
//
// One thing here is NOT disposable: the OFF-thread barrier around sfizz. The
// engine does not provide it (see SamplerEngine.h) and should not, because the
// barrier is a JUCE concept. Whatever replaces this file has to keep it.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#if KROMOSYNTH_HAS_SAMPLER
class SamplerEngine;
#endif

#if KROMOSYNTH_HAS_FAUST
// The full definition, not a forward declaration: applyFaustMeasurement takes
// FaustEngine::Measured, and a nested type cannot be forward-declared. Cheap to
// include -- FaustEngine.h pulls juce_audio_basics and four standard headers,
// and keeps wasmtime entirely inside its .cpp.
#include "FaustEngine.h"
#endif

#include "InstrumentLibrary.h"
#include "AcquisitionRunner.h"

class KromosynthAudioProcessor : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    KromosynthAudioProcessor();

    // Defined in the .cpp, not defaulted here. engine_ is a unique_ptr to an
    // incomplete type at this point, and ~unique_ptr needs the definition.
    ~KromosynthAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Without this override JUCE tells the AU wrapper that ANY output count is
    // supported, and auval duly reports channel capabilities of [0, -1] with:
    //
    //   WARNING: Source AU supports multi-channel output but does not provide
    //            a channel layout
    //
    // The warning is accurate and the declaration was the thing that was wrong.
    // SamplerEngine::process handles exactly two cases -- a stereo pair written
    // in place, and a mono bus downmixed through scratch -- and explicitly
    // clears any channel from index 2 upward because "the alternative to
    // clearing an unexpected channel is emitting whatever the host left in it".
    // So 4, 5, 6, 7 and 8 channel layouts were advertised and would have
    // produced silence in every channel past the second.
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Synth.is"; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // Read from sfizz at LOAD time and cached, so this message-thread query
    // never touches the introspection API. See SamplerEngine::tailSeconds().
    //
    // Returns the WORST case, which for both artefacts on disk is 8.3 s
    // (ampeg_release=0.3 plus ampeg_release_oncc73=8). The typical case at
    // default CC is 0.3 s, measured: a Reaper render is at exactly 0.0 from
    // one second after the last note-off. Reporting the worst case makes a
    // host render silence for the difference, which is the cheap direction to
    // be wrong in; the other direction truncates a real tail whenever CC73 is
    // up.
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Which instrument is loaded, so a saved project reopens playing it.
    //
    // Deliberately a ValueTree rather than a bare string. There are no plugin
    // parameters yet; when there are, an AudioProcessorValueTreeState's own
    // state becomes a CHILD of this tree and old sessions keep loading. A raw
    // string now would have to be migrated later.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Stage 0 instrumentation. The editor appends host-observable lifecycle
    // events here; see PluginEditor.cpp for where the log lands on disk.
    // Lives on the processor rather than the editor because the whole point
    // is to survive the editor being destroyed and recreated.
    void logEvent (const juce::String& event);

    // The only host-visible parameter. Owned by the AudioProcessor base after
    // addParameter; this is a borrowed pointer for reading it per block.
    juce::AudioParameterFloat* outputLevelDb_ = nullptr;

    // Envelope, applied by FaustEngine per voice. The sampler does not use
    // these: sfizz owns its voices and their envelopes, and there is no
    // per-voice gain to reach from outside -- shaping them means setting
    // ampeg_* opcodes at load, which is a different mechanism and a different
    // feel, so it is deliberately not pretended to be the same control.
    juce::AudioParameterFloat* attackS_ = nullptr;
    juce::AudioParameterFloat* decayS_ = nullptr;
    juce::AudioParameterFloat* sustain_ = nullptr;
    juce::AudioParameterFloat* releaseS_ = nullptr;

    // Experiment: does a host refresh a parameter's displayed name? Renamed on
    // load to the first slider the genome exposes, and does nothing else.
    // Typed as the base class so the header need not see RenamableParameter.
    juce::AudioParameterFloat* macro1_ = nullptr;

    // Exposed so async callbacks can capture the File BY VALUE and still log
    // after this processor is gone. juce::File is a value type holding a path
    // string — appending to it touches no live object, which is exactly what
    // the probe's editor-destroyed path needs.
    const juce::File& getLogFile() const noexcept { return logFile_; }

    // ── Shell vocabulary: declared unconditionally ──────────────────────────
    //
    // These two types belong to the shell, not to an engine. They were
    // declared inside the sampler guard back when the sampler was the only
    // engine, and that is how three of the four engine-option combinations
    // stopped compiling: `current_` in the SAMPLER||FAUST block needs
    // LoadOutcome, and PluginProcessor.cpp defines engineState() with no
    // guard, so any build without the sampler referenced types that were no
    // longer declared.
    //
    // Neither type mentions sfizz or Faust. Declaring them here is what stops
    // the option matrix rotting this way again.

    /** What a load attempt did. `ok` and `numRegions` are separate on purpose:
        F11 established that loadSfzFile returns true for an instrument whose
        every region was culled, so "it loaded" and "there is an instrument"
        are different claims and the UI must be able to tell them apart.
    */
    struct LoadOutcome
    {
        bool ok = false;
        int numRegions = 0;
        double tailSeconds = 0.0;
        juce::String id;
        juce::String detail;
    };

    /** Live engine state for the shell: notes in, notes out, voices sounding.
        Exists so "the note is stuck" can be told apart from "no note-off
        arrived" without attaching a debugger to a DAW.
    */
    struct EngineState { int notesOn = 0, notesOff = 0, activeVoices = 0, forcedStops = 0;
                         float trimDb = 0.0f, rawPeak = 0.0f, rtf = 0.0f;
                         // LIVE peak, distinct from rawPeak above. rawPeak is
                         // what the offline sweep measured; renderPeak is what
                         // the audio thread has actually produced since the
                         // instrument loaded. Without it, "I played and heard
                         // nothing" cannot be told apart from "we rendered
                         // signal and the host discarded it" -- which cost two
                         // diagnoses on 2026-09-08 and -09. FaustEngine has
                         // tracked both of these since §4.2 and nothing read
                         // them.
                         float renderPeak = 0.0f;
                         std::uint64_t renderBlocks = 0;
                         // Block cost as a fraction of the deadline. loadRecent
                         // is a rolling maximum, cleared each time it is read,
                         // so it behaves as a meter; loadMax and blocksOver are
                         // cumulative. blocksOver is only interpretable next to
                         // renderBlocks -- a count without a denominator says
                         // nothing about frequency.
                         float loadRecent = 0.0f, loadMax = 0.0f;
                         std::uint64_t blocksOver = 0;
                         float budgetMs = 0.0f;
                         int voiceCap = 0; bool faust = false; };

   #if KROMOSYNTH_HAS_SAMPLER
    /** Loads an instrument from the local library by id.

        Called on the MESSAGE thread, from a native function invoked by the
        shell's JavaScript. sfizz's loadSfzFile is OFF-annotated and that does
        not change because the caller is a webview, so this takes the same
        suspendProcessing barrier prepareToPlay does.
    */
    LoadOutcome loadInstrument (const juce::String& id);

    /** True while a Faust measurement is running on the background thread.

        The instrument is loaded but NOT yet levelled, and processing stays
        suspended until it is. See startLoadInstrument.
    */
    bool isMeasuring() const noexcept { return measuring_.load (std::memory_order_relaxed); }

    /** The result of the last load, valid once isMeasuring() is false.

        POLLED by the shell rather than pushed to it, for the reason
        AcquisitionRunner already records: a push has to cross threads into an
        editor that may have been destroyed since the work started, and a poll
        cannot.
    */
    LoadOutcome lastLoadOutcome() const;

    /** Stops everything now. Bound to a shell control because a DAW's own
        panic is not guaranteed to reach a plugin -- GarageBand has no visible
        one -- and a genome that ignores its gate can otherwise only be
        silenced by deleting the track.
    */
    void panic();

    /** Plays one note, from the message thread, without any MIDI hardware.

        DIAGNOSTIC, and the reason it exists is specific: on iOS the standalone
        app has no on-screen keyboard and no MIDI source unless one is attached,
        so there was no way to answer "does this make sound" at all. Loading an
        instrument proves the engine accepted it; only a note proves the audio
        path reaches the speaker.

        Engine-agnostic on purpose. The message is merged into the host's own
        MidiBuffer at the top of processBlock, so whichever engine is active
        receives it by exactly the route a host note takes -- rather than
        through a second path that could work when the real one does not.

        Not gated behind an engine macro: with no engine compiled in it queues a
        message that nothing consumes, which is harmless and still increments
        the block counter.
    */
    void triggerTestNote (int midiNote, float velocity, int durationMs);

    /** Moves an instrument to the Trash, unloading it first if it is the one
        currently playing.

        Unloading first is not tidiness. The engine holds the module bytes in
        memory, so a removed instrument goes on sounding exactly as before --
        the user would have deleted something still playing, and only found out
        at the next reload, on a file that is no longer there.
    */
    bool removeInstrument (const juce::String& id);

    EngineState engineState() const;

    /** What is loaded now. An empty id means the compiled-in default. */
    LoadOutcome currentInstrument() const { return current_; }

    const InstrumentLibrary& library() const noexcept { return library_; }
   #endif

    /** Downloads in flight and their outcomes.

        Lives here rather than on the editor so a download survives the editor
        being destroyed, which hosts do on every window close. See
        AcquisitionRunner.h.

        Outside the sampler guard on purpose. Acquiring an instrument is an HTTP
        fetch and a ZIP unpack into ~/Music; none of it involves sfizz. Guarding
        it would make downloads depend on an audio engine they never call --
        accidental coupling that stays invisible until someone builds without
        the sampler.
    */
    AcquisitionRunner& acquisitions() noexcept { return acquisitions_; }

private:
   #if KROMOSYNTH_HAS_SAMPLER
    // The compiled-in SFZ path, resolved in one place so the empty-path and
    // missing-file cases are answered once.
    juce::File sfzFile() const;

    void logRenderStats (const char* reason);

    // Created lazily on the first prepareToPlay, NOT in the constructor.
    //
    // Hosts instantiate during a scan and time out while doing it — Debug
    // measured 1532 ms under auval before there was any audio at all.
    // Deferring construction keeps the scan footprint exactly what Task 1
    // measured and makes every millisecond sfizz costs attributable to
    // activation instead.
    //
    // Mutated only inside the suspend window. The read on the audio thread is
    // not a data race: suspendProcessing and processBlock take the same
    // CriticalSection, and that supplies the happens-before edge. See
    // SamplerEngine.h.
    std::unique_ptr<SamplerEngine> engine_;
   #endif

   #if KROMOSYNTH_HAS_FAUST
    // The live Faust engine. Same lazy construction and same suspend
    // discipline as the sampler.
    //
    // ONE DIFFERENCE THAT MATTERS: sfizz retunes in place when the sample rate
    // changes (Synth.cpp:1073), so the sampler loads once and re-prepares.
    // Faust bakes the sample rate into the DSP struct at init(), and the block
    // size into the wasm memory layout, so a host that changes either forces a
    // full RELOAD. The paths are kept so that reload is possible without the
    // shell being asked again.
    std::unique_ptr<FaustEngine> faust_;
    juce::File faustWasm_, faustJson_;

    // What the artefact SAYS about itself, read from synthis.json at load.
    // Empty for instruments that predate the contract or were placed by hand,
    // which is why nothing here may be required.
    InstrumentLibrary::Metadata faustDeclared_;
    double faustRate_ = 0.0;
    int faustBlock_ = 0;

public:
    /** The loaded genome's spectrogram, or empty. See FaustEngine::Measured. */
    const std::vector<std::uint8_t>& faustSpectrogram() const noexcept { return faustSpectrogram_; }

private:
    void reloadFaustIfNeeded (double sampleRate, int samplesPerBlock);
    void applyFaustTrim();

   #if KROMOSYNTH_HAS_FAUST
    /** The half of applyFaustTrim that does NOT render: applies a measurement
        to the engine and to editor-visible state. Message thread only.
    */
    void applyFaustMeasurement (const FaustEngine::Measured& measured, double ms);

    /** Whether this load has to render its own measurement, or can take the
        artefact's numbers. One definition, shared by the load path and
        applyFaustTrim, because they must agree.
    */
    bool runtimeMeasurementNeeded() const;

    /** The measurement cached beside the instrument, if it is still valid.

        Measuring costs 105 seconds of rendered audio -- 1.2 to 4.8 s of wall
        clock -- and the result depends on nothing that changes between one
        selection of an instrument and the next. Switching back and forth
        re-derived the same numbers every time.

        VALIDITY IS THE WHOLE DESIGN. A stale cache here is worse than no cache:
        it would set the level of the keyboard from numbers measured under
        conditions that no longer hold, silently. So the file records everything
        the measurement depends on and is rejected unless all of it matches:

          runtime     wasmtime version plus OS and arch. A different runtime
                      can produce different samples, which is the entire reason
                      rtfPlatform exists in the artefact contract.
          sampleRate  peak IS a function of sample rate -- 48000 and 44100
                      differ by 0.66 dB overall and up to 3.8 dB at one pitch.
                      Block size is NOT, measured, so it is not recorded.
          module      size and hash of instrument.wasm. The artifactId is a
                      folder name and a re-download reuses it; the bytes are
                      what was actually measured.

        Kept beside the instrument rather than in one central file so that
        removing an instrument takes its cache with it, and named .local because
        it belongs to THIS machine -- it is not part of the artefact and must
        never be mistaken for the platform's own declared numbers, which stay in
        synthis.json and remain available as a cross-check.
    */
    std::optional<FaustEngine::Measured> cachedMeasurement() const;

    /** Writes the measurement beside the instrument for next time. */
    void writeCachedMeasurement (const FaustEngine::Measured& measured) const;

    /** measured.local.json for the currently loaded Faust instrument. */
    juce::File cacheFileForCurrentFaust() const;
   #endif

    float faustTrim_ = 1.0f, faustRawPeak_ = 0.0f;

    // Log-frequency spectrogram of the loaded genome, from the measurement
    // render. Empty when nothing is loaded, when the artefact's own numbers
    // were trusted so no render happened, or when the genome was silent.
    std::vector<std::uint8_t> faustSpectrogram_;
    double faustRtf_ = 0.0;
    int faustVoices_ = 1;
   #endif

    // ── Shell state: declared unconditionally ───────────────────────────────
    //
    // Read by the editor on every poll, whichever engine is active and whether
    // or not one is at all. measuring_ and the outcome pair used to sit inside
    // the FAUST guard while their accessors -- isMeasuring() and
    // lastLoadOutcome() -- sat inside the SAMPLER guard, so a sampler-only
    // build declared the readers and not the members. That is the error the
    // first iOS build hit.
    //
    // lifetime_ is here for the same reason: PluginProcessor.cpp captures a
    // weak_ptr to it in four callbacks and none of them is guarded.

    // Set while a measurement runs on measurePool_. Read by the editor. Stays
    // false when no engine that measures is compiled in.
    std::atomic<bool> measuring_ { false };

    // Blocks processBlock has been called with, since construction. Engine
    // independent on purpose -- see the increment for why the engine's own
    // counter cannot answer "is this host pulling audio at all".
    std::atomic<std::uint64_t> blocksSeen_ { 0 };

    // BLOCK LOAD: wall time spent in processBlock as a fraction of the time the
    // block represents. 1.0 means the deadline was exactly consumed.
    //
    // This is the quantity the whole voice-cap policy is a PROXY for, measured
    // directly. capForRtf derives a cap from a free-running RTF and then divides
    // by four, and the comment on that measurement says plainly that free
    // running is not a headroom claim about a busy DAW -- d3321de measured a
    // real block cadence costing 2.3-2.9x more, and that penalty varies BY
    // GENOME, so the proxy misallocates in both directions and nothing
    // distinguishes which genomes it under-serves.
    //
    // Measured here rather than in an engine because the host budgets
    // processBlock, not our render call.
    //
    // Three numbers rather than one, because a single all-time maximum is
    // useless: one cold-cache block after a resume would read as "over budget"
    // for the rest of the session. The recent maximum is a live meter, and the
    // over-budget COUNT against blocksSeen_ is what says whether an overrun was
    // a one-off or a pattern -- 1 block in 100000 is noise, 500 is a problem.
    std::atomic<float> blockLoadRecent_ { 0.0f };
    std::atomic<float> blockLoadMax_ { 0.0f };
    std::atomic<std::uint64_t> blocksOverBudget_ { 0 };

    // FRAMES THE HOST ACTUALLY DELIVERED, which is not getBlockSize().
    //
    // getBlockSize() is the host's DECLARED MAXIMUM, set at prepareToPlay. AUM
    // declares 2048 and delivers 256, so the deadline reported to the editor
    // was 42.67 ms while the deadline the recorder measured against was 5.33 ms
    // -- an eightfold overstatement of the time available, sitting next to a
    // correct percentage.
    //
    // Measured on device 2026-09-10: the footer read "load 35% of 42.67 ms"
    // in the same second the log read "budget_ms=5.33 frames=256". Both came
    // from this class. A reader multiplying 35% by 42.67 gets 14.9 ms for a
    // block that took 1.87 ms.
    //
    // This is the other half of why C22's numbers could not be read, and the
    // half nobody was looking for: the over-budget capture answered "which
    // blocks", and the denominator beside them was wrong the whole time.
    std::atomic<int> lastBlockFrames_ { 0 };

    // ── load as a function of voice count ───────────────────────────────
    //
    // C27 answered whether an overrun is a load transient or the genome: the
    // genome. It did NOT produce the number the cap decision rests on, and
    // load_max cannot -- it is one figure with no voice count attached, so it
    // says nothing about the slope.
    //
    // This does. One note, then two, then three gives the intercept and the
    // per-voice cost directly, and where the line crosses 1.0 is the polyphony
    // the host can actually sustain -- MEASURED, rather than inferred from a
    // free-running RTF and a 4x divisor. Replacing that inference is the point.
    //
    // A COUNT BESIDE EVERY MAX, because a max drawn from three blocks and a max
    // drawn from four thousand are different claims that look identical. The
    // registered bar (AUV3_IOS_FINDINGS C29) requires at least 200 blocks at
    // the cap before a reading counts, and without this column that condition
    // cannot be checked at all.
    static constexpr int kMaxTrackedVoices = 32;

    std::atomic<float> loadByVoices_[kMaxTrackedVoices + 1] {};
    std::atomic<std::uint32_t> blocksByVoices_[kMaxTrackedVoices + 1] {};

    // WARMUP, skipped rather than filtered out afterwards.
    //
    // The bar excludes transients, and a maximum cannot exclude anything
    // retroactively: one cold block at three voices would poison entry 3 for
    // the rest of the session and there would be no way to tell. So the first
    // blocks after a load never enter the table.
    //
    // 64 blocks is 0.34 s at AUM's 256-frame delivery. C27 measured the real
    // overruns at block 7,400 and beyond, so this window drops the cold start
    // WITHOUT dropping anything the measurement is about -- which is the test a
    // warmup window has to pass to be honest rather than merely convenient.
    static constexpr std::uint64_t kLoadTableWarmupBlocks = 64;

    std::atomic<std::uint64_t> blocksSinceLoad_ { 0 };

    /** The load-by-voice table as one sparse log field. Message thread.

        Sparse on purpose: voice counts never seen are omitted rather than
        printed as zero, so the line shows what was actually played instead of
        33 columns of nothing.
    */
    juce::String loadByVoicesForLog() const;

    /** " cap=N" for the log, or empty when no Faust instrument is loaded.

        The cap belongs beside the table rather than only on the instrument_load
        line: the bar is a statement about the entry AT the cap, so a reader
        needs both without scrolling to find which load this was.
    */
    juce::String faustVoiceCapForLog() const;

    // WHICH blocks went over, and not merely how many.
    //
    // C22's first readings -- peak 142%, 22 OVER BUDGET of 4065 -- cannot tell a
    // heavy genome from one late block after a load, and that is the entire
    // question the meter was built to answer. A count says a thing happened.
    // These say WHEN, at what block size, and with how many voices sounding,
    // which is what makes an overrun correlatable against the instrument_load
    // line sitting above it in the same file.
    //
    // FIRST N, not a ring. The transient hypothesis is specifically about the
    // first blocks after the suspend barrier lifts on a load, so a ring would
    // overwrite exactly the evidence being sought. Overruns past the last slot
    // are still COUNTED, by overBudgetClaimed_, so the slots can never be
    // mistaken for the total.
    static constexpr int kOverBudgetCaptures = 8;

    struct OverBudgetBlock
    {
        std::int64_t ticks = 0;         // getHighResolutionTicks at the overrun
        float load = 0.0f;              // fraction of the deadline consumed
        float elapsedMs = 0.0f;         // what the block actually cost
        // What it had. RECORDED rather than recomputed at drain time: a host
        // that changed sample rate or block size in between would otherwise
        // make this line describe a deadline the block never had.
        float budgetMs = 0.0f;
        int frames = 0;                 // the host's block size, 64 to 2048
        int voices = 0;                 // voices sounding at end of block
        std::uint64_t blockIndex = 0;   // blocksSeen_ when it happened
    };

    // Plain and non-atomic, which is correct rather than sloppy: the audio
    // thread writes each slot exactly once, and overBudgetPublished_ carries
    // the release edge that makes the write visible to the drain.
    OverBudgetBlock overBudgetBlocks_[kOverBudgetCaptures] {};

    // claimed counts overruns SINCE THE LAST LOAD, including ones with no slot
    // left to write into, so it is the denominator the slot lines are read
    // against -- 3 of 3 is three one-offs, 3 of 800 is a pattern whose first
    // three are merely all that was kept. published is how many slots are
    // safely filled. drained is how many the timer has logged, and is
    // message-thread only.
    std::atomic<std::uint32_t> overBudgetClaimed_ { 0 };
    std::atomic<std::uint32_t> overBudgetPublished_ { 0 };
    std::uint32_t overBudgetDrained_ { 0 };

    /** Logs any captured over-budget blocks not yet logged. Message thread.

        Writes nothing when there is nothing new, so it can be called
        unconditionally -- which it MUST be. The midi_in gate in timerCallback
        returns early when no MIDI moved, and the transient this exists to
        catch arrives with no MIDI at all.
    */
    void drainOverBudgetBlocks();

    /** Discards the measurement window and starts a new one, so that every
        reading belongs to the instrument whose load preceded it.

        Covers BOTH the over-budget captures and the load-by-voice table. They
        rearm together because they answer one question between them and a table
        spanning two instruments would silently average a cheap genome with an
        expensive one.

        Message thread, and ONLY from inside a suspendProcessing window: it
        writes counters the audio thread also writes, and the barrier is what
        makes that safe -- the same discipline engine_ and faust_ are held to.

        Drains first, so rearming never silently loses a line. Does NOT touch
        blocksOverBudget_, which is the plugin's lifetime count and is what the
        status line and over= report.
    */
    void rearmLoadMeasurement();

    /** Records one block's cost. Audio thread, and nothing else calls it.

        Two mach_absolute_time reads and at most three relaxed atomic writes on
        the ordinary path. No allocation, no locks, no syscall -- the same
        standard blocksSeen_ and FaustEngine's peak_ already meet.

        An OVER-BUDGET block costs more: a third clock read, a voice-count loop
        over at most 32 idle flags, seven plain stores and one release
        increment. Still allocation-free and lock-free, and the block that pays
        it has already missed its deadline -- so the extra work lands where it
        can no longer make the difference, which is the only reason it is
        acceptable at all.
    */
    void recordBlockLoad (int frames, std::int64_t ticks) noexcept;

    // Notes injected by triggerTestNote, drained on the audio thread.
    //
    // A plain MidiBuffer under a lock rather than juce::MidiMessageCollector,
    // which would have been the obvious choice: it lives in juce_audio_devices
    // and this target links juce_audio_processors, so using it meant adding a
    // whole module to the shared code for one diagnostic. The queue it replaces
    // is four lines.
    //
    // The audio thread takes this with a TRY lock, so a note waits one block
    // rather than the render thread ever waiting on the message thread.
    juce::MidiBuffer pendingTestNotes_;
    juce::CriticalSection testNoteLock_;

    // ── MIDI arrival diagnostics ────────────────────────────────────────────
    //
    // Written on the audio thread, read and logged on the message thread by
    // timerCallback, and logged ONLY when a counter moves so an idle plugin
    // writes nothing.
    //
    // These exist because "notes arrive 1-2 s late and every other one is
    // silent" has several possible causes that are indistinguishable by ear:
    // the messages arriving late, arriving on time but placed at the wrong
    // offset within the block, or a note-on and its note-off landing in the
    // SAME block -- which is a note of zero length, and silent however good
    // the engine is. zeroLength_ is the one that tells those apart.
    std::atomic<std::uint64_t> midiEvents_    { 0 };
    std::atomic<std::uint64_t> midiNoteOns_   { 0 };
    std::atomic<std::uint64_t> midiNoteOffs_  { 0 };
    std::atomic<std::uint64_t> midiZeroLength_ { 0 };
    std::atomic<int> midiLastNote_   { -1 };
    std::atomic<int> midiLastOffset_ { -1 };
    std::atomic<int> midiMaxPerBlock_ { 0 };
    std::uint64_t midiLoggedAt_ { 0 };

    void timerCallback() override;

public:
    /** How many blocks the host has asked for. 0 means no audio callback has
        ever run, whatever is or is not loaded. */
    std::uint64_t blocksSeen() const noexcept
    { return blocksSeen_.load (std::memory_order_relaxed); }

    /** " render_peak=... render_blocks=...", or "" when there is no Faust
        engine loaded to ask.

        Returns a fragment rather than logging, so it can be appended to the
        midi_in line instead of adding a second line that would have to be
        correlated with the first by timestamp.
    */
    juce::String faustRenderStatsForLog() const;

private:

    // Guarded by the message thread: written when a load finishes, read by
    // lastLoadOutcome() which the shell polls.
    LoadOutcome lastOutcome_;
    mutable juce::CriticalSection outcomeLock_;

    // A host can close a plugin between setStateInformation posting the load
    // and the message thread running it. AudioProcessor has no weak reference,
    // so the lambda holds a weak_ptr to this and does nothing if it expired.
    std::shared_ptr<int> lifetime_ { std::make_shared<int> (0) };

   #if KROMOSYNTH_HAS_SAMPLER || KROMOSYNTH_HAS_FAUST
    // Which engine processBlock should call. Message-thread writes happen only
    // inside the suspend window; see prepareToPlay.
    enum class Active { none, sampler, faust };
    Active active_ = Active::none;

    InstrumentLibrary library_;

    // Message-thread only.
    LoadOutcome current_;

    // setStateInformation and prepareToPlay arrive in either order, and which
    // one a host picks is not something to rely on. Whichever runs second does
    // the load; this is how they find each other.
    juce::CriticalSection pendingLock_;
    juce::String pendingInstrumentId_;

    void loadPendingInstrument();

   #endif

    AcquisitionRunner acquisitions_;

    juce::File logFile_;

    // DECLARED LAST ON PURPOSE, after logFile_ as well as after faust_. Members
    // are destroyed in reverse order, so this pool is torn down first -- and
    // its destructor waits for a running job -- before anything that job
    // touches: the engine it renders through and the log file it writes to.
    // Declaring it one line earlier would leave a measurement running against
    // a destroyed File.
    //
    // ONE thread: two measurements at once would render through the same
    // engine, the same voices and the same mix buffer.
    juce::ThreadPool measurePool_ { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KromosynthAudioProcessor)
};
