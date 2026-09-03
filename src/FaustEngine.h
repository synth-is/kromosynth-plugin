#pragma once

// FaustEngine — Stage 2's walking skeleton. A live Faust instrument.
//
// ── What this is, and what the sampler is not ────────────────────────────────
//
// SamplerEngine plays a rendered artefact: sfizz, an SFZ file, a directory of
// WAVs. That is a delivery path kromosynth-live already has. This is the job
// nothing else in the platform does — MIDI in, synthesis out, no samples
// anywhere. It is the "grown, not sampled" claim in playable form.
//
// ── Everything load-bearing here was established by EXPERIMENT_01 ────────────
//
// The ABI is not invented here. `probe/gate0/host_wasm3.c` drove these exact
// modules and its output was bit-identical to the platform reference across
// four independent engines. What follows is that host, in C++, with a voice
// allocator on top. Where a choice looks arbitrary it is almost certainly
// copied from a measurement.
//
// Three things carried over verbatim, each because getting it wrong is silent:
//
// 1. **The math imports compute in DOUBLE.** faustwasm binds `_powf` to
//    JavaScript's `Math.pow`, which is double precision and narrows to f32
//    only on the return into wasm. A host that calls `powf()` computes the
//    whole thing in float and lands on a different sample. Measured divergence
//    on a ten-import elite: **-50.5 dB** — small, audible, and exactly the bug
//    that ships an instrument not matching its own preview.
//    EXPERIMENT_01_3_DESKTOP_RESULT §1 calls this the single most load-bearing
//    implementation detail for Stage 2.
//
// 2. **The full import table is linked, not the measured union.** §4.1 found
//    the union over a 120-elite archive to be 11 functions, but the set is
//    program-dependent: a term the search has not reached yet could import one
//    no measured elite needed. wasm3 returns m3Err_functionLookupFailed for a
//    name the module does not import, which is the normal case and not an
//    error. An unused link costs nothing; a missing one is a hard failure at
//    load, on a user's machine, for a genome nobody tested.
//
// 3. **One runtime + module + memory per voice.** The mono factory's memory
//    holds one DSP struct at offset 0, so N voices means N instances. §5
//    measured the cost of this against ideal linear scaling at 1–4%, so the
//    simple thing is also very nearly the fast thing.
//
// ── Runtime choice ──────────────────────────────────────────────────────────
//
// wasmtime/Cranelift, pinned to v48.0.1 — the exact build EXPERIMENT_01
// measured, verified by libwasmtime.a hashing identically to the copy that
// measurement used. Over the 120-elite archive it beats the wasm3 interpreter
// on single-voice RTF by min 2.02x, median 5.56x, max 24.49x, which at the
// >=4x bar is a median of 16 voices against 4.
//
// This does not follow VENDORING.md, which permits only pinned submodules
// consumed by add_subdirectory; wasmtime ships as a prebuilt tarball and
// building it from source needs Rust. cmake/Wasmtime.cmake overrides the rule
// while keeping the reason behind it, by pinning a release and verifying its
// SHA256 at configure time.
//
// iOS forbids JIT and will need an interpreter, so a second backend is coming.
// It is not abstracted for yet: this class is the seam, and the abstraction
// gets built when there is a second real consumer rather than an imagined one.
//
// ── Not done yet, deliberately ──────────────────────────────────────────────
//
// No acquisition, no artefact format, no site changes. The artefact path is
// handed in. Those follow once this makes a sound.

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

struct FaustVoice;
struct FaustEngineImpl;

class FaustEngine
{
public:
    FaustEngine();
    ~FaustEngine();

    struct LoadResult
    {
        bool ok = false;
        juce::String detail;
        int structBytes = 0;
        int outputs = 0;
        int voices = 0;
        juce::String dspName;
        // Empty when the module publishes no `gate` -- see hasGate() below.
        bool hasGate = false, hasFreq = false, hasGain = false;
    };

    // `wasmFile` is the Faust-emitted module; `jsonFile` is the DSP metadata
    // beside it, which carries the struct size and the parameter byte offsets.
    // Both are required: the layout below cannot be derived from the module
    // alone, which is why the Gate 0 probe was handed a .plan rather than
    // parsing one.
    LoadResult load (const juce::File& wasmFile,
                     const juce::File& jsonFile,
                     double sampleRate,
                     int maxBlockSize,
                     int numVoices);

    void reset() noexcept;

    // Renders into `buffer`, which may have one or two channels. Faust modules
    // from this substrate declare `outputs: 1`, so the mono result is copied
    // across the pair rather than downmixed -- the opposite direction from
    // SamplerEngine, and worth stating because the two engines look symmetric
    // and are not.
    void process (juce::AudioBuffer<float>& buffer,
                  const juce::MidiBuffer& midi) noexcept;

    bool isLoaded() const noexcept { return loaded_; }

    // Sets a genome parameter on every voice. `byteOffset` is the `index` field
    // from the DSP JSON's ui tree, which is an offset into the DSP struct
    // rather than an ordinal -- Faust names it "index" and it is not one.
    //
    // Public because the genome's own controls (bodyDec, drive, rvmix and
    // whatever else a given genome evolved) are what the plugin will expose as
    // automatable parameters. The null test uses it to replay a Gate 0 .plan
    // through the exact parameter path the plugin will use, rather than through
    // a separate one that could be right while the shipping path is wrong.
    void setParameter (int byteOffset, float value) noexcept;

    /** Every control the genome publishes.

        NOT a fixed set. Surveyed over the 120-elite archive: freq appears on
        100%, gate on 95%, lvl on 93%, gain on 75% -- the polyphony convention
        is near-universal. Everything else is the template the genome was grown
        from showing through: dec on 31%, exdec 23%, att and rel on 20% each,
        bodyDec on 16%, sus on 7%.

        So a plugin cannot offer "the decay knob". It can offer whatever this
        genome happens to have, which is what an automation surface has to be
        built against.
    */
    struct Parameter
    {
        juce::String label;
        int   byteOffset = 0;
        float init = 0.0f, minimum = 0.0f, maximum = 1.0f;
        bool  isButton = false;
    };

    const std::vector<Parameter>& parameters() const noexcept;

    /** Silences every voice immediately, gate or no gate.

        reset() closes the gate and trusts the genome to fall silent. A
        MEASURED 16 of 33 archive elites do not: they publish a `gate`
        parameter and ignore it, still sounding 2-3 s after gate-off, the worst
        at only -6.7 dB below its own peak. On such a genome reset() is not a
        panic, it is a request the DSP may decline.

        This is the one that cannot be declined -- it re-runs init(), which
        zeroes the DSP struct. The instrument is playable again afterwards; it
        just forgets everything, which for a stuck drone is the point.
    */
    void panic() noexcept;

    // §4.2's guard, carried into the plugin. A synth that renders silence
    // never misses a deadline, never crashes, and passes every validator.
    // auval's own render tests only check that rendering does not crash.
    float peakSinceReset() const noexcept { return peak_.load (std::memory_order_relaxed); }
    std::uint64_t blocksRendered() const noexcept { return blocks_.load (std::memory_order_relaxed); }

    /** How many voices the plugin had to stop itself because the genome would
        not. Zero on a genome that articulates; non-zero is not an error, it is
        the drone-handling working -- but it is a behaviour change from the
        platform's own render and should never be invisible.
    */
    int forcedStops() const noexcept;

    /** MIDI CC64. Without this a host whose sustain is engaged produces notes
        that never stop, and it looks identical to a broken note-off.
    */
    void setSustain (bool down) noexcept;

    /** Counted at both ends, because a stuck note has two possible causes --
        the engine mishandled a note-off, or none arrived -- and they need
        completely different fixes. notesOn far exceeding notesOff means the
        host is not sending them.
    */
    /** Renders this genome offline at several pitches and returns the loudest
        peak it produced.

        Three pitches, not one. Measured over 120 archive genomes, peak spread
        across c3/a4/a5 has a median of 3.8 dB and a maximum of 24.2 dB, and
        44 of the 120 would have their trim wrong by more than 6 dB from a
        single pitch. The direction is inconsistent -- some are loudest low,
        some high -- so there is no safe pitch to pick.

        Leaves the engine reset. Costs one short render per pitch.
    */
    struct Measured
    {
        float  peak = 0.0f;      ///< loudest across the probed pitches
        double rtf = 0.0;        ///< single-voice real-time factor, free-running
        int    capAt4x = 1;      ///< voices sustainable at the §3.3 headroom bar

        /** Per-pitch peaks at c3, a4 and a5, which the overall peak hides.

            Carried because ONE number cannot level this substrate. Measured
            2026-09-03 on 01M1JD666ZTWGTCT4W2GDTVQMF: c3 11.3854, a4 0.3992,
            a5 2.4816 — a 29.1 dB spread across the keyboard. A single output
            gain set from the loudest pitch makes that genome inaudible at a4;
            set from the quietest it clips by 29 dB at c3.

            ARTEFACT_NUMBERS put the spread at a median of 3.8 dB and a maximum
            of 24.2 dB over 120 archive genomes. This one is past that maximum,
            so the figure describes the older archive rather than the substrate.
        */
        float peakC3 = 0.0f, peakA4 = 0.0f, peakA5 = 0.0f;

        /** Measured peak at every kSweepStep semitones from kSweepFirstNote.

            A TABLE, not three points and a curve. Measured 2026-09-03 on
            01M1JD666ZTWGTCT4W2GDTVQMF, peak against pitch is not smooth:

              midi 48  11.39      midi 66   0.396
              midi 54   0.378     midi 69   0.399
              midi 60   0.351     midi 72   3.466   <- resonance
              midi 63   0.340     midi 81   2.482

            Interpolating the three reference pitches in log frequency puts midi
            72 at 0.63 against an actual 3.47, so the correction
            under-attenuates and the note is delivered at 2.2 and clips. At midi
            54 the same curve reads 21.3 dB HIGH and attenuates the note into
            near-silence. A model fitted to three points is right at three
            points.
        */
        std::vector<float> pitchPeaks;

        /** Coarse log-frequency spectrogram of the a4 probe, or empty.

            kSpectrogramFrames rows of kSpectrogramBins bytes, row-major, each
            byte a magnitude in dB mapped from -60..0 onto 0..255.

            FREE, which is the only reason it is here. measure() already renders
            five seconds at this pitch and throws all of it away except the
            peak; an FFT over samples that were produced anyway costs no extra
            rendering. Fetching a picture from the platform instead would put a
            network request on the one surface whose whole justification is
            working without a network.

            It is also the RIGHT picture: this is the instrument as this machine
            plays it, at the host's sample rate, rather than a render of the
            same genome made elsewhere under other conditions.
        */
        std::vector<std::uint8_t> spectrogram;
    };

    static constexpr int kSpectrogramBins = 128;
    static constexpr int kSpectrogramFrames = 192;

    /** The pitch sweep behind Measured::pitchPeaks.

        Covers midi 36..96, C2 to C7 -- A KEYBOARD, not a comfortable middle.
        It was 48..81, and outside that range the estimate was clamped to the
        edge entry and then still allowed to boost: a note at midi 36 was
        estimated at the midi 48 level and amplified 8x, delivering 4.1 against
        a target of 1.0. The first key on a GarageBand keyboard is C2.

        Three semitones is a compromise measured rather than guessed. Validated
        over 10 archive genomes at EVERY semitone in range, the worst delivered
        peak is 1.909, and 9 of the 10 never exceed full scale at all. The one
        that does has a resonance at midi 58 taller than the entries either side
        of it, which no interpolation of a 3-semitone grid can see; halving the
        step would catch that one at double the load-time cost. The output
        ceiling covers the residual.
    */
    static constexpr int kSweepFirstNote = 36;   // c2
    static constexpr int kSweepStep = 3;
    static constexpr int kSweepCount = 21;       // 36..96, and 48/69/81 land on it

    /** Renders this genome offline at several pitches and reports peak and RTF.

        Three pitches, not one. Measured over 120 archive genomes, peak spread
        across c3/a4/a5 has a median of 3.8 dB and a maximum of 24.2 dB, and 44
        of the 120 would have their trim wrong by more than 6 dB from a single
        pitch. The direction is inconsistent -- some are loudest low, some high
        -- so there is no safe pitch to pick.

        RTF comes free: it is the wall clock of the render the peak needed.

        Leaves the engine reset.
    */
    Measured measure();

    /** Identifies the wasm runtime this build measures on.

        ARTEFACT_CONTRACT §3.2: an artefact carries `rtf` with the platform it
        was measured on, and a consumer may only reuse that figure when the
        platform matches its own. Measured V8/wasmtime ratios run 0.48-0.99
        across six genomes, tracking genome cost, so there is no factor that
        converts one runtime's figure into another's.

        Derived from WASMTIME_VERSION rather than written out, so bumping the
        runtime invalidates stored figures automatically. A hardcoded string
        would keep matching after an upgrade and the plugin would go on
        trusting numbers measured by a different engine.

        An iOS build that ships an interpreter instead of wasmtime must report
        a different id here, or it will accept desktop figures that are 2-24x
        optimistic for it.
    */
    static juce::String runtimeId();

    /** Changes polyphony without recompiling the module. */
    bool setVoiceCount (int numVoices);

    /** Per-note gain so every key delivers the level the preview delivered.

        THE PROBLEM. The site previews a sound by rendering it at ONE pitch and
        attenuating only if it exceeds full scale. The plugin played the same
        genome at a single output gain chosen from the LOUDEST of three pitches.
        For a genome with a 29 dB pitch spread those disagree by 31 dB at the
        very pitch the user auditioned — the preview's own note.

        THE RULE. Every note is scaled so its peak matches the peak at the
        reference pitch, and the reference pitch is delivered at exactly the
        level the preview delivers. Playing the audition note reproduces the
        audition; playing any other note gives the same instrument at the same
        loudness rather than a different one.

        Peaks come from the MEASURED TABLE, not from a curve fitted to three
        points: see Measured::pitchPeaks for why that is wrong here. Between
        entries the estimate is interpolated in dB; outside the table it is
        clamped rather than extrapolated.

        Applied at the VOICE OUTPUT, not through the `gain` parameter. `gain` is
        the genome's own widget and an evolved program may route it into a
        nonlinearity, so scaling there could change the timbre instead of only
        the level. Scaling the samples cannot.

        An empty table disables it, which is the default: a parameter-driven
        render — measure() itself — must never be compensated, or the
        measurement would be of its own correction.
    */
    void setPitchCompensation (const std::vector<float>& pitchPeaks, float referencePeak);

    /** Soft-limits the summed output. OFF by default, and that matters.

        Per-note correction cannot guarantee the sum stays inside full scale:
        voices ADD, and a resonance narrower than the sweep's three-semitone
        spacing can sit between two table entries and be under-attenuated. On
        01M1JD666ZTWGTCT4W2GDTVQMF the resonance at midi 72 is 14.8 dB above
        what a three-point fit predicted, which is audible as harsh clipping.

        tanh, not a hard clip: both bound the signal, but hard clipping adds a
        discontinuity and the odd harmonics that go with it. Below about half
        scale tanh is within a fraction of a dB of unity, so a single note at
        the preview's level passes through essentially untouched.

        OFF by default because measure() renders through the same path and would
        report a peak the ceiling invented, and because faust-probe's null test
        compares raw samples against the platform reference. Only playback turns
        it on.
    */
    void setOutputCeiling (bool shouldLimit) noexcept;

    /** The user's output level, applied BEFORE the limiter.

        Before, not after, and that is the whole reason it exists as an engine
        setting rather than a gain on the buffer afterwards: turning it down
        must move the signal OUT of limiting, not merely make the limited result
        quieter.

        It is the visible half of a conflict that cannot be resolved silently.
        Matching the preview means one note at full velocity reaches full scale,
        which means two notes do not fit. A default with headroom and a control
        that reaches unity gives both, and lets the person choose which they
        want rather than having a constant chosen for them.
    */
    void setOutputLevel (float linearGain) noexcept;

    /** Applied to the summed output. 1.0 is off, and off is the default: the
        null test compares raw output against the platform reference and must
        not be silently scaled. Only the processor sets this.
    */
    void setOutputGain (float g) noexcept;
    float outputGain() const noexcept;

    int notesOn() const noexcept;
    int notesOff() const noexcept;
    int activeVoices() const noexcept;

private:
    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void setParam (FaustVoice& v, int byteOffset, float value) noexcept;
    bool instantiateVoices (int numVoices, juce::String& err);

    std::unique_ptr<FaustEngineImpl> impl_;

    bool loaded_ = false;
    std::atomic<float> peak_ { 0.0f };
    std::atomic<std::uint64_t> blocks_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaustEngine)
};
