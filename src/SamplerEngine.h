// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// ─────────────────────────────────────────────────────────────────────────────
// SamplerEngine — sfizz behind a JUCE-shaped surface.
//
// FIRST PASS. Not the Stage 1 design, and the places where it is not are
// marked STAGE 1 rather than left for someone to discover.
//
// There is deliberately NO engine abstraction. The top-level CMakeLists.txt
// says not to grow one before there is a second engine to abstract over, and
// there still is not: the Faust engine is gated on EXPERIMENT_01, which is at
// "protocol, pre-registered" with the measurement not yet taken. This is one
// concrete class, and when a second engine arrives the shared shape will be
// obvious from two implementations instead of guessed from one.
//
// ── Thread-safety contract ───────────────────────────────────────────────────
//
// sfizz annotates its API. Two categories matter here (sfizz.hpp:56-62):
//
//   RT   must be called from the same task as renderBlock
//   OFF  must NOT be called while any thread is inside an RT function
//
// This class does NOT provide the OFF barrier. Its caller does, because the
// barrier is a JUCE concept (AudioProcessor::suspendProcessing) and an engine
// that reached up into the processor to suspend it would be harder to test and
// harder to reuse. Methods below are annotated with which category they are in
// and the processor is where the guarantee is made. See PluginProcessor.cpp.
//
// ── State that crosses threads ───────────────────────────────────────────────
//
// Two different kinds, handled two different ways, on purpose:
//
//   • Configuration (loaded_, numRegions_, preparedBlock_, the sfizz instance)
//     is written on the OFF thread inside the processor's suspend window and
//     read on the audio thread. These are PLAIN members, not atomics, and that
//     is not an oversight. JUCE's AU and VST3 wrappers hold
//     AudioProcessor::getCallbackLock() across the whole processBlock call
//     (juce_audio_plugin_client_AU_1.mm:2189, _VST3.cpp:3714) and
//     suspendProcessing takes that same lock (juce_AudioProcessor.cpp:583).
//     The lock is what establishes happens-before; adding atomics on top would
//     imply the lock argument is not trusted, which would be worse than either.
//
//   • Instrumentation (blocksRendered_, framesRendered_, peak_) is written on
//     the AUDIO thread and read from releaseResources and the destructor,
//     neither of which is guaranteed to hold the callback lock. Those are
//     genuinely racy and are genuinely atomic.
//
// ── Why the render is chunked ────────────────────────────────────────────────
//
// Not defensive coding. sfizz renders SILENCE, with no Release-build
// diagnostic, if numFrames exceeds the size last given to setSamplesPerBlock:
//
//   Synth::renderBlock zeroes the output buffer first (Synth.cpp:1102), then
//   asks BufferPool::getStereoBuffer(numFrames). BufferPool sizes its buffers
//   to exactly the last setBufferSize and returns an empty holder when asked
//   for more (BufferPool.h:133-136). renderBlock then returns at
//   Synth.cpp:1130-1133 behind a bare DBG(), which is compiled out in Release.
//
// Same trap a second time: setSamplesPerBlock carries
// ASSERT(samplesPerBlock <= config::maxBlockSize) with maxBlockSize 8192
// (Config.h:25), and ASSERT is also a no-op in Release (utility/Debug.h:60).
//
// So an oversized block is a silent instrument with an empty log, twice over.
// EXPERIMENT_01_DSP_EXECUTION.md section 4.2 again: a runtime that renders
// silence is infinitely fast. Chunking costs about ten lines and one branch in
// the common case, where the chunk count is 1.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <sfizz.hpp>
#include <sfizz_message.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class SamplerEngine
{
public:
    /** Shapes the amplitude envelope of the loaded SFZ, through the CC
        modulation the artefact already declares.

        SFZ DOES support this and I said otherwise -- sfizz owns its voices, but
        `hdcc` is the documented way in, and sfzGenerator.js has been emitting
        the mappings all along:

            ampeg_attack_oncc70=4     ampeg_decay_oncc71=8
            ampeg_sustain_oncc72=-base  ampeg_release_oncc73=8

        Nothing overrides `envelopeCCRanges`, so those defaults are what every
        artefact carries and the mapping below is exact rather than assumed.

        The values ADD to what the artefact baked in, so 0 means "as generated"
        -- the same reading as the Faust engine, where a flat envelope leaves
        the genome as rendered. Sustain is the exception in the SFZ's own terms:
        its range is negative, so more CC means less sustain, and the mapping
        here inverts it so that the control still reads high-is-more.
    */
    void setEnvelope (float attackS, float decayS, float sustain, float releaseS) noexcept;

    SamplerEngine();
    ~SamplerEngine();

    // Everything the caller needs to log a load, including the two numbers
    // that distinguish the three ways a load can go wrong.
    struct LoadResult
    {
        bool ok = false;         // what loadSfzFile returned
        int numRegions = 0;      // what actually survived — see below
        int numVoices = 0;       // sfizz's polyphony, logged not remembered
        double loadMs = 0.0;
        double tailMs = 0.0;
        double tailSeconds = 0.0;
        juce::String detail;
    };

    /** OFF. Caller must guarantee no RT call is in flight.

        Checks getNumRegions() > 0, not just the boolean. STAGE0_FINDINGS.md
        F11: loadSfzFile returns TRUE when every region was culled for an
        unresolvable sample, because culling happens after parsing
        (Synth.cpp:901). Two different on-disk artefact layouts exist (E5), so
        a broken default_path is a live failure mode rather than a
        hypothetical, and it would present as an instrument that loads
        perfectly and plays nothing.
    */
    LoadResult load (const juce::File& sfzFile);

    /** OFF. Caller must guarantee no RT call is in flight. */
    void prepare (double sampleRate, int maxBlockSize);

    /** RT. */
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

    /** RT. */
    void reset() noexcept;

    bool isLoaded() const noexcept   { return loaded_; }
    int  numRegions() const noexcept { return numRegions_; }

    /** Any thread. Worst-case release across every region, in seconds.

        Read from sfizz ONCE at load time and cached, because the processor
        needs it from `getTailLengthSeconds()` on the message thread and the
        introspection API is documented RT.

        The standing rule is "do not call the introspection API from the
        message thread", and this does not break it. The rule's actual content
        is that `sendMessage` must not run concurrently with `renderBlock`;
        `measureTailSeconds()` is called from inside `load()`, which the caller
        already runs inside the OFF barrier, where no RT call can be in flight.
        That is exactly the condition that makes the sfz-probe harness safe.
    */
    double tailSeconds() const noexcept
    {
        return tailSeconds_.load (std::memory_order_relaxed);
    }

    // Instrumentation drain. Answers "did this thing ever render anything",
    // which is the only question that separates silence-because-not-loaded
    // from silence-because-every-region-was-culled without a DAW in the loop.
    struct RenderStats
    {
        std::uint64_t blocks = 0;
        std::uint64_t frames = 0;
        float peak = 0.0f;

        // Largest chunk count seen in one process() call. 1 means the chunk
        // loop has never actually looped, and the defence against the
        // oversized-block silence is therefore UNEXERCISED rather than
        // working. Reported so that distinction is visible instead of assumed.
        int maxChunks = 0;

        // Largest block the host asked for. Compared against the prepared
        // size, this is what says whether any host in the matrix violates the
        // "samplesPerBlock is a maximum" contract.
        int maxHostBlock = 0;
    };

    RenderStats stats() const noexcept;

private:
    void dispatch (int delay, const juce::MidiMessage& message) noexcept;

    /** OFF. Only valid inside the caller's suspend window. */
    double measureTailSeconds (int regionCount);

    sfz::Sfizz synth_;

    // Last CC values sent for the envelope, so an unchanged control is not
    // re-sent every block. sfizz treats each hdcc as an event to schedule.
    float lastEnvCc_[4] = { -1.0f, -1.0f, -1.0f, -1.0f };

    // Note-ons seen, and the most recent note number. Read by the shell so a
    // silent sampler can be told apart: notes arriving and no voices sounding
    // means the key is outside the SFZ's mapped range, while no notes at all
    // means they never reached the plugin.
    std::atomic<int> notesReceived_ { 0 };
    std::atomic<int> lastNote_ { -1 };

public:
    int notesReceived() const noexcept { return notesReceived_.load (std::memory_order_relaxed); }
    int lastNote() const noexcept { return lastNote_.load (std::memory_order_relaxed); }
    int activeVoices() const noexcept { return synth_.getNumActiveVoices(); }

    /** Lowest and highest key the loaded SFZ actually maps. A generated
        instrument covers only the pitches that were rendered.
    */
    juce::Range<int> mappedKeyRange() const noexcept { return mappedKeys_; }

private:
    juce::Range<int> mappedKeys_ { -1, -1 };

    // Only used when the host hands us a mono bus. Sized in prepare().
    juce::AudioBuffer<float> scratch_;

    // Configuration — see the header comment on why these are not atomic.
    int preparedBlock_ = 0;
    double preparedRate_ = 0.0;
    bool loaded_ = false;
    int numRegions_ = 0;

    // Written on the OFF thread at load, read from the message thread by
    // getTailLengthSeconds. Genuinely concurrent with nothing in particular,
    // so genuinely atomic.
    std::atomic<double> tailSeconds_ { 0.0 };

    // Instrumentation — genuinely cross-thread, genuinely atomic.
    std::atomic<std::uint64_t> blocksRendered_ { 0 };
    std::atomic<std::uint64_t> framesRendered_ { 0 };
    std::atomic<float> peak_ { 0.0f };
    std::atomic<int> maxChunks_ { 0 };
    std::atomic<int> maxHostBlock_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerEngine)
};
