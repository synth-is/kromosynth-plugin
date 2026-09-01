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
// ── Runtime choice, and why it is behind this interface ──────────────────────
//
// wasm3, an interpreter, not wasmtime — despite Gate 0 finding Cranelift
// 2.3–2.9x faster and naming it the desktop engine. Two reasons:
//
//   - wasmtime's C API ships as a prebuilt tarball and building it from source
//     needs a Rust toolchain. VENDORING.md permits exactly one mechanism,
//     pinned git submodules consumed by add_subdirectory, and it says why:
//     the reference repo it was written against had an uninitialised submodule
//     and a build that worked on one machine and nowhere else.
//   - A skeleton needs to make a noise, not to be fast.
//
// wasm3 is twelve dependency-free .c files and fits the rule exactly. The
// runtime sits behind this class so swapping it is contained; nothing above
// FaustEngine knows which engine is underneath.
//
// ── Not done yet, deliberately ──────────────────────────────────────────────
//
// No acquisition, no artefact format, no site changes. The artefact path is
// handed in. Those follow once this makes a sound.

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>

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

    // §4.2's guard, carried into the plugin. A synth that renders silence
    // never misses a deadline, never crashes, and passes every validator.
    // auval's own render tests only check that rendering does not crash.
    float peakSinceReset() const noexcept { return peak_.load (std::memory_order_relaxed); }
    std::uint64_t blocksRendered() const noexcept { return blocks_.load (std::memory_order_relaxed); }

private:
    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void setParam (FaustVoice& v, int byteOffset, float value) noexcept;

    std::unique_ptr<FaustEngineImpl> impl_;

    bool loaded_ = false;
    std::atomic<float> peak_ { 0.0f };
    std::atomic<std::uint64_t> blocks_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaustEngine)
};
