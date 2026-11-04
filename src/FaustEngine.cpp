// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// FaustEngine.cpp — see the header for why each decision here is what it is.
//
// The wasm3 driving code is a port of probe/gate0/host_wasm3.c, which produced
// bit-identical output to the platform reference across the 120-elite archive.
// Divergence from that file should be treated as a bug in this one.

#include "FaustEngine.h"

#if KROMOSYNTH_WASM3
// The interpreter backend, for platforms that forbid JIT. wasm3.h brings
// m3ApiRawFunction and friends with it, as host_wasm3.c relies on.
//
// Both runtimes are declared even when only one is compiled in, because the
// wasmtime types appear in FaustVoice's non-wasm3 branch and the file has to
// parse either way.
#include "wasm3.h"
#else
#include <wasm.h>
#include <wasmtime.h>
#endif

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <functional>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// The env import table.
//
// DOUBLE precision throughout, mirroring what faustwasm's createWasmImport
// actually does in a browser: it binds these to JavaScript Math.*, which is
// double, and narrows to f32 only on the return into wasm. Calling the float
// libm here instead is the obvious C choice and it is wrong by up to -50.5 dB.
// EXPERIMENT_01_3_DESKTOP_RESULT §1.
// ─────────────────────────────────────────────────────────────────────────────

// wasmtime raw-slot callbacks rather than boxed wasmtime_val_t. host_wasmtime.c
// records why: the boxed path would penalise wasmtime against V8, whose import
// calls are cheap, and produce a slower-looking JIT for a reason that has
// nothing to do with code quality. §2.5 measured that boundary crossing as the
// dominant cost on math-heavy elites, so it is not a micro-optimisation.
//
// TWO SETS OF WRAPPERS, ONE SET OF EXPRESSIONS.
//
// wasm3's raw-function ABI is not wasmtime's, so the wrappers cannot be shared.
// The EXPRESSIONS are, via the FE_MATH_EXPRESSIONS list below, because they are
// the part that must not drift: every one computes in DOUBLE and narrows on the
// way out, because the reference these renders are null-tested against reaches
// libm through JavaScript -- faustwasm's createWasmImport binds `_powf` to
// Math.pow, which is double precision. A host that calls powf() in float lands
// somewhere else. host_wasm3.c measured -50.5 dB on a math-heavy elite from
// exactly that mistake.
//
// So the rule is: add a function to FE_MATH_EXPRESSIONS, never to one wrapper
// set alone.
#define UNARY(NAME, EXPR)                                                     \
    static wasm_trap_t* NAME (void*, wasmtime_caller_t*,                      \
                              wasmtime_val_raw_t* a, size_t) {                \
        const float x = a[0].f32; a[0].f32 = (float) (EXPR); return nullptr; }
#define BINARY(NAME, EXPR)                                                    \
    static wasm_trap_t* NAME (void*, wasmtime_caller_t*,                      \
                              wasmtime_val_raw_t* a, size_t) {                \
        const float x = a[0].f32, y = a[1].f32;                               \
        a[0].f32 = (float) (EXPR); return nullptr; }

// The expressions, once. Both wrapper sets and both link tables are generated
// from this list, so a function cannot exist on one runtime and not the other,
// and cannot compute differently on each.
//
// U(name, wasmName, expr) / B(name, wasmName, expr)
#define FE_MATH_EXPRESSIONS(U, B)                                              \
    U (fe_abs,   "_abs",     std::fabs   ((double) x))                         \
    U (fe_acos,  "_acosf",   std::acos   ((double) x))                         \
    U (fe_asin,  "_asinf",   std::asin   ((double) x))                         \
    U (fe_atan,  "_atanf",   std::atan   ((double) x))                         \
    U (fe_ceil,  "_ceilf",   std::ceil   ((double) x))                         \
    U (fe_cos,   "_cosf",    std::cos    ((double) x))                         \
    U (fe_exp,   "_expf",    std::exp    ((double) x))                         \
    U (fe_floor, "_floorf",  std::floor  ((double) x))                         \
    U (fe_log,   "_logf",    std::log    ((double) x))                         \
    U (fe_log10, "_log10f",  std::log10  ((double) x))                         \
    U (fe_round, "_roundf",  std::round  ((double) x))                         \
    U (fe_sin,   "_sinf",    std::sin    ((double) x))                         \
    U (fe_sqrt,  "_sqrtf",   std::sqrt   ((double) x))                         \
    U (fe_tan,   "_tanf",    std::tan    ((double) x))                         \
    U (fe_acosh, "_acoshf",  std::acosh  ((double) x))                         \
    U (fe_asinh, "_asinhf",  std::asinh  ((double) x))                         \
    U (fe_atanh, "_atanhf",  std::atanh  ((double) x))                         \
    U (fe_cosh,  "_coshf",   std::cosh   ((double) x))                         \
    U (fe_sinh,  "_sinhf",   std::sinh   ((double) x))                         \
    U (fe_tanh,  "_tanhf",   std::tanh   ((double) x))                         \
    B (fe_atan2, "_atan2f",  std::atan2  ((double) x, (double) y))             \
    B (fe_fmod,  "_fmodf",   std::fmod   ((double) x, (double) y))             \
    B (fe_pow,   "_powf",    std::pow    ((double) x, (double) y))             \
    /* NOT std::remainder: the JS table defines _remainderf as                 \
       x - Math.round(x/y)*y, and Math.round rounds halves upward where        \
       remainder() rounds half to even. Transcribed from the thing being       \
       matched rather than substituted for it. */                              \
    B (fe_remainder, "_remainderf",                                            \
       (double) x - std::round ((double) x / (double) y) * (double) y)         \
    /* Precision-independent: exact for f32 inputs under either scheme. */     \
    U (fe_isnan,    "_isnanf",    std::isnan (x) ? 1.0f : 0.0f)                \
    U (fe_isinf,    "_isinff",    std::isinf (x) ? 1.0f : 0.0f)                \
    B (fe_max,      "_max_f",     (x > y ? x : y))                             \
    B (fe_min,      "_min_f",     (x < y ? x : y))                             \
    B (fe_copysign, "_copysignf", std::copysign (x, y))

#if ! KROMOSYNTH_WASM3
#define FE_DEFINE_U(NAME, WASMNAME, EXPR) UNARY  (NAME, EXPR)
#define FE_DEFINE_B(NAME, WASMNAME, EXPR) BINARY (NAME, EXPR)
FE_MATH_EXPRESSIONS (FE_DEFINE_U, FE_DEFINE_B)
#undef FE_DEFINE_U
#undef FE_DEFINE_B
#endif

#if KROMOSYNTH_WASM3
// The same expressions again, in wasm3's raw-function ABI. Transcribed from
// probe/gate0/host_wasm3.c, which produced the reference numbers this backend
// is null-tested against -- so the argument order, the return type and the
// double-precision narrowing are copied rather than re-derived.
#define FE3_U(NAME, WASMNAME, EXPR)                                            \
    static m3ApiRawFunction (NAME##_m3) {                                      \
        m3ApiReturnType (float);                                               \
        m3ApiGetArg (float, x);                                                \
        m3ApiReturn ((float) (EXPR));                                          \
    }
#define FE3_B(NAME, WASMNAME, EXPR)                                            \
    static m3ApiRawFunction (NAME##_m3) {                                      \
        m3ApiReturnType (float);                                               \
        m3ApiGetArg (float, x);                                                \
        m3ApiGetArg (float, y);                                                \
        m3ApiReturn ((float) (EXPR));                                          \
    }
FE_MATH_EXPRESSIONS (FE3_U, FE3_B)
#undef FE3_U
#undef FE3_B
#endif

namespace
{
#if ! KROMOSYNTH_WASM3
struct Link { const char* name; int arity; wasmtime_func_unchecked_callback_t fn; };

// Generated from FE_MATH_EXPRESSIONS, not maintained beside it. The hand-kept
// version and the expression list were two places to add a function to, and
// only one of them would fail loudly if you forgot.
#define FE_LINK_U(NAME, WASMNAME, EXPR) { WASMNAME, 1, NAME },
#define FE_LINK_B(NAME, WASMNAME, EXPR) { WASMNAME, 2, NAME },
const Link kMathTable[] = {
    FE_MATH_EXPRESSIONS (FE_LINK_U, FE_LINK_B)
};
#undef FE_LINK_U
#undef FE_LINK_B
#endif

#if KROMOSYNTH_WASM3
struct Link3 { const char* name; const char* sig; M3RawCall fn; };

// Signatures are wasm3's: "f(f)" is float(float). Every Faust math import is
// f32 in, f32 out, which is why two shapes cover all 29.
#define FE3_LINK_U(NAME, WASMNAME, EXPR) { WASMNAME, "f(f)",  NAME##_m3 },
#define FE3_LINK_B(NAME, WASMNAME, EXPR) { WASMNAME, "f(ff)", NAME##_m3 },
const Link3 kMathTable3[] = {
    FE_MATH_EXPRESSIONS (FE3_LINK_U, FE3_LINK_B)
};
#undef FE3_LINK_U
#undef FE3_LINK_B

// Every voice gets its own runtime, so this is per voice rather than per
// module. 64 KB matches host_wasm3.c's default; the archive's deepest genome
// did not approach it.
constexpr std::uint32_t kWasm3StackBytes = 64 * 1024;
#endif

#if ! KROMOSYNTH_WASM3   // builds wasmtime functypes; wasm3 declares arity in a signature string
wasm_functype_t* funcType (int arity)
{
    wasm_valtype_vec_t params, results;

    if (arity == 1)
    {
        wasm_valtype_t* p[1] = { wasm_valtype_new_f32() };
        wasm_valtype_vec_new (&params, 1, p);
    }
    else
    {
        wasm_valtype_t* p[2] = { wasm_valtype_new_f32(), wasm_valtype_new_f32() };
        wasm_valtype_vec_new (&params, 2, p);
    }

    wasm_valtype_t* r[1] = { wasm_valtype_new_f32() };
    wasm_valtype_vec_new (&results, 1, r);
    return wasm_functype_new (&params, &results);
}
#endif

float mtof (int note) noexcept
{
    return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
}

/** One step of a linear gain ramp. */
inline float rampToward (float current, float target, float step) noexcept
{
    if (current == target) return target;
    const float delta = target - current;
    return std::abs (delta) <= step ? target
                                    : current + (delta > 0.0f ? step : -step);
}

/** Level at which the output ceiling starts to act. Below this it is EXACTLY
    unity, which is the whole point.
*/
constexpr float kCeilingKnee = 0.8f;

/** Level at which the output limiter starts to act. */
constexpr float kLimiterThreshold = 0.95f;

/** Bounds a sample to full scale, shaping only what is above the knee.

    KEPT as the last-resort clamp behind the limiter, not as the limiter. A
    bare tanh() over the whole signal was the first version and it was the
    wrong tool: tanh is a SATURATOR, nonlinear everywhere, so tanh(1.0) = 0.762
    is 2.4 dB of shaping applied to every peak of every note. Once the levelling
    became correct, every pitch arrived at exactly 1.0 and went through the
    saturator's worst region at once -- the instrument got audibly more
    distorted the moment the maths got right.

    Behind a limiter it only ever sees the limiter's attack overshoot, which is
    a sample or two.
*/
inline float softCeiling (float x) noexcept
{
    const float a = std::abs (x);

    if (a <= kCeilingKnee) return x;

    const float over = (a - kCeilingKnee) / (1.0f - kCeilingKnee);
    return std::copysign (kCeilingKnee + (1.0f - kCeilingKnee) * std::tanh (over), x);
}

/** Short-time Fourier magnitudes of a capture, on a log frequency scale.

    Returns kSpectrogramFrames rows of kSpectrogramBins bytes, or empty when
    there is nothing to show.

    LOG frequency, not linear. A linear map spends three quarters of its height
    above 5 kHz, where an evolved genome usually has little, and crushes the
    octaves that carry its character into the bottom few pixels. The point of
    this picture is to tell two instruments apart at a glance.

    Normalised to the loudest bin in the capture rather than to an absolute
    level, because the raw peaks span 40 dB across the archive and an absolute
    scale would render most instruments as an empty rectangle.
*/
std::vector<std::uint8_t> buildSpectrogram (const std::vector<float>& capture, double sampleRate)
{
    constexpr int kBins = FaustEngine::kSpectrogramBins;
    constexpr int kFrames = FaustEngine::kSpectrogramFrames;
    constexpr int kFftOrder = 10;                 // 1024 samples, ~21 ms at 48 k
    constexpr int kFftSize = 1 << kFftOrder;

    if (capture.size() < (size_t) kFftSize || sampleRate <= 0.0)
        return {};

    juce::dsp::FFT fft (kFftOrder);

    // Hann, computed once. Without a window every frame carries the spectral
    // leakage of a rectangular edge and a tonal genome smears across the map.
    std::vector<float> window ((size_t) kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) (kFftSize - 1));

    const int usable = (int) capture.size() - kFftSize;
    const double hop = (double) usable / (double) (kFrames - 1);

    // Output bin -> source range, log-spaced from 40 Hz to Nyquist. 40 rather
    // than 0 because the bottom bins are dominated by DC and offset.
    const double fMin = 40.0;
    const double fMax = sampleRate * 0.5;
    const double hzPerBin = sampleRate / (double) kFftSize;

    std::vector<float> fftBuffer ((size_t) kFftSize * 2);
    std::vector<float> magnitudes ((size_t) kFrames * kBins, 0.0f);
    float loudest = 0.0f;

    for (int f = 0; f < kFrames; ++f)
    {
        const int start = (int) std::llround ((double) f * hop);

        std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            fftBuffer[(size_t) i] = capture[(size_t) (start + i)] * window[(size_t) i];

        fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

        for (int b = 0; b < kBins; ++b)
        {
            // Geometric spacing: each output bin covers a constant RATIO of
            // frequency, so an octave occupies the same height wherever it sits.
            const double lo = fMin * std::pow (fMax / fMin, (double) b / (double) kBins);
            const double hi = fMin * std::pow (fMax / fMin, (double) (b + 1) / (double) kBins);

            int srcLo = (int) std::floor (lo / hzPerBin);
            int srcHi = (int) std::ceil (hi / hzPerBin);

            srcLo = juce::jlimit (0, kFftSize / 2 - 1, srcLo);
            // At least one source bin: the lowest output bins are narrower than
            // the FFT's resolution, and an empty range would draw a black stripe
            // across the bottom of every instrument.
            srcHi = juce::jlimit (srcLo + 1, kFftSize / 2, srcHi);

            float peakHere = 0.0f;
            for (int s = srcLo; s < srcHi; ++s)
                peakHere = juce::jmax (peakHere, fftBuffer[(size_t) s]);

            magnitudes[(size_t) f * kBins + (size_t) b] = peakHere;
            loudest = juce::jmax (loudest, peakHere);
        }
    }

    if (loudest <= 0.0f) return {};

    std::vector<std::uint8_t> out ((size_t) kFrames * kBins, 0);

    for (size_t i = 0; i < magnitudes.size(); ++i)
    {
        const float db = 20.0f * std::log10 (juce::jmax (1.0e-6f, magnitudes[i] / loudest));
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        out[i] = (std::uint8_t) juce::jlimit (0, 255, (int) std::lround (norm * 255.0f));
    }

    return out;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────

struct FaustVoice
{
#if KROMOSYNTH_WASM3
    // ONE RUNTIME PER VOICE, and that is forced rather than chosen.
    // m3_LoadModule consumes the module and the runtime owns the instance's
    // linear memory, so voices cannot share a parsed module the way they share
    // a Cranelift-compiled one. The cost is a parse per voice at load, which
    // host_wasm3.c measured at about 3 ms -- small, but it scales with voice
    // count where wasmtime's instantiation does not.
    IM3Runtime  rt  = nullptr;
    IM3Module   mod = nullptr;      // owned by rt once loaded; do not free
    IM3Function fInit = nullptr, fCompute = nullptr, fSetParam = nullptr;
#else
    wasmtime_store_t*   store = nullptr;
    wasmtime_context_t* ctx   = nullptr;
    wasmtime_instance_t instance {};
    wasmtime_func_t     fInit {}, fCompute {}, fSetParam {};
#endif

    std::uint8_t* memory = nullptr;
    std::size_t   memBytes = 0;

    std::uint32_t dspPtr = 0, inPtrArr = 0, outPtrArr = 0, inBufs = 0, outBufs = 0;

    int  note = -1;          // -1 means retriggerable
    bool gateOpen = false;
    std::uint64_t startedAt = 0;   // for voice stealing: oldest first

    // ── forced release ──────────────────────────────────────────────────
    // 16 of 33 archive genomes ignore their own gate (ELITE_ARTICULATION
    // 2026-09-01). Closing the gate is a request they decline, so the plugin
    // has to be able to stop a voice itself. What it must NOT do is truncate a
    // genome whose tail is legitimately long -- an eight-second pad and a flat
    // drone both "still sound" a second after note-off, and the difference is
    // that one of them is getting quieter.
    bool  releasing = false;
    int   framesSinceRelease = 0;
    float peakAtRelease = 0.0f;    // level in the block the note-off landed in
    float peakRecent = 0.0f;       // level in the most recent block
    bool  forcing = false;         // fading out because it would not stop
    float forceGain = 1.0f;

    // An idle voice is not computed at all.
    //
    // Necessary, not merely an optimisation. Re-running init() zeroes the DSP
    // struct, which stops a genome that needs its gate -- but some genomes
    // SELF-OSCILLATE from a zeroed state and simply start again.
    // elite_16_deep_perc_c7-31 does: it was force-stopped, re-initialised, and
    // came straight back at 0.538. Nothing done to the struct can stop a
    // genome that needs no input to make sound; the only thing that works is
    // not calling compute().
    bool idle = true;
    int  quietBlocks = 0;

    // Whether this voice is being driven by MIDI or by raw setParameter calls.
    //
    // The auto-idle below keys off "gate closed and silent", and gate is only
    // ever opened by noteOn -- so a parameter-driven voice looks permanently
    // gate-closed and idles itself the moment it has a quiet block. Genomes
    // with a slow attack begin with several. That silently broke 28 of the 33
    // null tests, which is how it was found.
    bool noteDriven = false;

    // Per-note level correction, 1.0 until a noteOn sets it. Parameter-driven
    // voices keep 1.0, which is what stops measure() from measuring its own
    // compensation.
    //
    // TWO values, because this cannot be stepped. noteOff sets note = -1 while
    // the voice is still RINGING, so noteOn picks that voice as free and would
    // change its correction underneath sound that is still playing. On a genome
    // with a 29 dB pitch spread that is a 29 dB instantaneous step, and it
    // clicks -- inconsistently, because it depends on which voice was picked
    // and whether it was still audible.
    float compGain = 1.0f, compTarget = 1.0f;

    // A note waiting for this voice to fade out before it starts. See noteOn:
    // reusing a voice that is still sounding is a 1.0 to 1.2 boundary jump, and
    // the fade is what removes it.
    int pendingNote = -1, pendingVelocity = 0;
    bool retriggering = false;

    // The plugin's own amplitude envelope, MULTIPLYING the genome's rather than
    // replacing it.
    //
    // It cannot replace it: the envelope is inside the compiled DSP, shaped by
    // the seed's own parameters, and nothing in the wasm ABI reaches in to
    // change it. So this can lengthen an attack but never shorten one, and can
    // cut a tail short but never extend it.
    //
    // That asymmetry is still worth having, because the one thing it CAN do is
    // the thing most needed: 16 of the 33 measured genomes ignore their gate
    // entirely and sound forever. Today those are stopped by a grace period and
    // a blunt 0.2 s fade. A release envelope makes them playable instead of
    // merely stoppable.
    enum class Env { off, attack, decay, sustain, release };
    Env envStage = Env::off;
    float envLevel = 0.0f;

    // Held by the sustain pedal after its key came up. The note number is
    // cleared so the same key can retrigger, but the voice keeps sounding
    // until the pedal lifts.
    bool sustained = false;
};

/** Zeroes a voice's DSP struct. The one thing a genome cannot decline. */
inline void initVoice (FaustVoice& v, double sampleRate) noexcept
{
#if KROMOSYNTH_WASM3
    m3_CallV (v.fInit, (std::int32_t) v.dspPtr, (std::int32_t) sampleRate);
#else
    wasmtime_val_raw_t args[2];
    args[0].i32 = (std::int32_t) v.dspPtr;
    args[1].i32 = (std::int32_t) sampleRate;

    wasm_trap_t* trap = nullptr;
    if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fInit, args, 2, &trap))
        wasmtime_error_delete (err);
    if (trap != nullptr) wasm_trap_delete (trap);
#endif
}

struct FaustEngineImpl
{
    // ONE module, compiled once by Cranelift, shared across every voice --
    // unlike the wasm3 path, where each voice had to parse its own because
    // m3_LoadModule consumes the module and it owns the instance's memory.
    // Compilation happens in wasmtime_module_new and is the expensive step, so
    // sharing it is what keeps per-voice load cost at instantiation only.
#if KROMOSYNTH_WASM3
    // No shared module here: see FaustVoice. The environment is shared, the
    // module is not.
    IM3Environment env = nullptr;
#else
    wasm_engine_t*      engine = nullptr;
    wasmtime_module_t*  module = nullptr;
    wasmtime_linker_t*  linker = nullptr;
#endif
    std::vector<std::uint8_t> wasm;
    std::vector<FaustVoice> voices;

    int structBytes = 0, inputs = 0, outputs = 1;
    int maxBlockSize = 0;
    double sampleRate = 48000.0;

    // Byte offsets into the DSP struct. -1 when the module does not publish
    // the parameter, which is legal: not every evolved genome carries a gate.
    int idxFreq = -1, idxGain = -1, idxGate = -1;

    juce::String dspName;
    std::vector<FaustEngine::Parameter> params;
    std::uint64_t counter = 0;

    // Scratch for the mono module's output before it reaches the buffer.
    std::vector<float> mix;

    bool sustainDown = false;
    float outputGain = 1.0f;

    // The soft ceiling. DEFAULT OFF, and that is not a detail: measure() renders
    // through process() and would report a squashed peak instead of the
    // genome's real one, and faust-probe's null test compares raw samples
    // against the platform reference, which a ceiling would alter above roughly
    // 0.2. Only playback turns it on.
    bool ceiling = false;

    // The limiter's current gain, and its one-pole coefficients.
    //
    // A LIMITER, not a waveshaper. Measured over 12 genomes from the
    // timbre-seeded run, the worst delivered peak after levelling is still
    // 14.9 -- 23 dB over -- because a resonance can hide between sweep entries.
    // Shaping each sample individually at that depth turns the waveform into a
    // square; reducing the GAIN of the whole signal instead sounds like heavy
    // compression, which is what an instrument 23 dB too hot should sound like.
    float limiterGain = 1.0f;
    float limiterAttack = 0.0f, limiterRelease = 0.0f;

    // User-facing output level, applied BEFORE the limiter so that turning it
    // down moves the signal out of limiting rather than merely making the
    // limited result quieter.
    std::atomic<float> outputLevel { 1.0f };

    // Envelope shape, in per-sample terms. Recomputed when the parameters or
    // the sample rate change.
    std::atomic<float> envAttackInc { 1.0f };
    std::atomic<float> envDecayInc { 1.0f };
    std::atomic<float> envSustain { 1.0f };
    std::atomic<float> envReleaseCoef { 0.0f };

    // Measured peaks at c3, a4, a5. All zero means compensation is off.
    float peakC3 = 0.0f, peakA4 = 0.0f, peakA5 = 0.0f;

    // The sweep table and the level every note is corrected towards. Empty
    // table means no compensation.
    std::vector<float> pitchPeaks;
    float referencePeak = 0.0f;

    // Gain units per sample for the correction ramp: a full 0 to 1 traversal in
    // 5 ms. Long enough that a step is inaudible, short enough that it is over
    // before any musically meaningful part of the attack.
    float compRampStep = 1.0f;

    // Diagnostics. A stuck note has two possible causes -- the engine mishandled
    // a note-off, or no note-off arrived -- and they need completely different
    // fixes. Counting both ends answers which without a debugger.
    std::atomic<int> notesOn { 0 }, notesOff { 0 }, sustainEvents { 0 };

    // Which allocator branch supplied each note's voice. See noteOn: from
    // outside the engine a reuse and a steal look identical, so this is the
    // only place the distinction exists.
    std::atomic<int> startsIdle { 0 }, startsReused { 0 }, startsStolen { 0 };

    int graceFrames = 0, fadeFrames = 0;
    std::atomic<int> forcedStops { 0 };
};

FaustEngine::FaustEngine() : impl_ (std::make_unique<FaustEngineImpl>()) {}

FaustEngine::~FaustEngine()
{
    if (impl_ == nullptr) return;

#if KROMOSYNTH_WASM3
    // m3_FreeRuntime releases the runtime's module and its linear memory, so
    // there is nothing else per voice to release. The environment goes last.
    for (auto& v : impl_->voices)
        if (v.rt != nullptr) m3_FreeRuntime (v.rt);

    if (impl_->env != nullptr) m3_FreeEnvironment (impl_->env);
#else
    for (auto& v : impl_->voices)
        if (v.store != nullptr) wasmtime_store_delete (v.store);

    if (impl_->linker != nullptr) wasmtime_linker_delete (impl_->linker);
    if (impl_->module != nullptr) wasmtime_module_delete (impl_->module);
    if (impl_->engine != nullptr) wasm_engine_delete (impl_->engine);
#endif
}

// Creates N instances from the ALREADY-COMPILED module. Separated from load()
// so the voice count can change without paying Cranelift again -- which is what
// makes measuring a genome at one voice and then sizing the polyphony to the
// result affordable.
bool FaustEngine::instantiateVoices (int numVoices, juce::String& errOut)
{
#if KROMOSYNTH_WASM3
    for (auto& v : impl_->voices)
        if (v.rt != nullptr) m3_FreeRuntime (v.rt);   // frees its module too
#else
    for (auto& v : impl_->voices)
        if (v.store != nullptr) wasmtime_store_delete (v.store);
#endif

    impl_->voices.clear();
    impl_->voices.resize ((size_t) numVoices);

#if KROMOSYNTH_WASM3
    for (auto& v : impl_->voices)
    {
        v.rt = m3_NewRuntime (impl_->env, kWasm3StackBytes, nullptr);
        if (v.rt == nullptr) { errOut = "m3_NewRuntime failed"; return false; }

        // Its OWN parse of the bytes. m3_LoadModule takes the module, so one
        // parsed module cannot be loaded into two runtimes.
        if (auto r = m3_ParseModule (impl_->env, &v.mod,
                                     impl_->wasm.data(), (std::uint32_t) impl_->wasm.size()))
        { errOut = juce::String ("m3_ParseModule: ") + r; return false; }

        if (auto r = m3_LoadModule (v.rt, v.mod))
        { errOut = juce::String ("m3_LoadModule: ") + r; return false; }

        // AFTER load, not before. m3_LinkRawFunction on an unloaded module
        // returns "attempting to use module that is not loaded" -- the module
        // has to belong to a runtime before its imports can be bound. This is
        // the order host_wasm3.c uses, and getting it backwards fails at the
        // first import rather than subtly.
        //
        // Failures for names the module does not import are normal:
        // m3_LinkRawFunction returns m3Err_functionLookupFailed for those, and
        // an unused link costs nothing.
        for (const auto& m : kMathTable3)
        {
            auto r = m3_LinkRawFunction (v.mod, "env", m.name, m.sig, m.fn);
            if (r != nullptr && r != m3Err_functionLookupFailed)
            { errOut = juce::String ("link ") + m.name + ": " + r; return false; }
        }

        if (m3_FindFunctionIn (&v.fInit,     v.mod, "init") != nullptr)
        { errOut = "missing export: init"; return false; }
        if (m3_FindFunctionIn (&v.fCompute,  v.mod, "compute") != nullptr)
        { errOut = "missing export: compute"; return false; }
        if (m3_FindFunctionIn (&v.fSetParam, v.mod, "setParamValue") != nullptr)
        { errOut = "missing export: setParamValue"; return false; }

        std::size_t memBytesSz = 0;
        v.memory = m3_GetMemory (v.mod, &memBytesSz, 0);
        if (v.memory == nullptr) { errOut = "module exports no memory"; return false; }
        v.memBytes = memBytesSz;
#else
    for (auto& v : impl_->voices)
    {
        v.store = wasmtime_store_new (impl_->engine, nullptr, nullptr);
        v.ctx = wasmtime_store_context (v.store);

        wasm_trap_t* trap = nullptr;
        if (auto* err = wasmtime_linker_instantiate (impl_->linker, v.ctx, impl_->module,
                                                     &v.instance, &trap))
        { wasmtime_error_delete (err); errOut = "instantiate failed"; return false; }
        if (trap != nullptr) { wasm_trap_delete (trap); errOut = "instantiate trapped"; return false; }

        const auto getFunc = [&] (const char* name, wasmtime_func_t& out) -> bool
        {
            wasmtime_extern_t item;
            if (! wasmtime_instance_export_get (v.ctx, &v.instance, name, std::strlen (name), &item)
                || item.kind != WASMTIME_EXTERN_FUNC)
                return false;
            out = item.of.func;
            return true;
        };

        if (! getFunc ("init", v.fInit))               { errOut = "missing export: init"; return false; }
        if (! getFunc ("compute", v.fCompute))         { errOut = "missing export: compute"; return false; }
        if (! getFunc ("setParamValue", v.fSetParam))  { errOut = "missing export: setParamValue"; return false; }

        wasmtime_extern_t memItem;
        if (! wasmtime_instance_export_get (v.ctx, &v.instance, "memory", 6, &memItem)
            || memItem.kind != WASMTIME_EXTERN_MEMORY)
        { errOut = "module exports no memory"; return false; }

        v.memory = wasmtime_memory_data (v.ctx, &memItem.of.memory);
        v.memBytes = wasmtime_memory_data_size (v.ctx, &memItem.of.memory);
#endif

        // Layout, identical to layoutMono() in host_wasmtime.c and to
        // FaustMonoWebAudioDsp.initMemory. -single only: §4.1 established
        // sampleSize 4 across the archive, and a -double module would silently
        // mis-stride here, so the fit is checked rather than assumed.
        const std::uint32_t ss = 4, ps = 4;
        v.dspPtr    = 0;
        v.inPtrArr  = (std::uint32_t) impl_->structBytes;
        v.outPtrArr = v.inPtrArr + (std::uint32_t) impl_->inputs * ps;
        v.inBufs    = v.outPtrArr + (std::uint32_t) impl_->outputs * ps;
        v.outBufs   = v.inBufs + (std::uint32_t) impl_->inputs * (std::uint32_t) impl_->maxBlockSize * ss;

        const std::uint32_t end =
            v.outBufs + (std::uint32_t) impl_->outputs * (std::uint32_t) impl_->maxBlockSize * ss;

        if ((std::size_t) end > v.memBytes)
        {
            errOut = "layout " + juce::String ((int) end) + " B exceeds module memory "
                     + juce::String ((int) v.memBytes) + " B at block size "
                     + juce::String (impl_->maxBlockSize);
            return false;
        }

        auto* h32 = reinterpret_cast<std::uint32_t*> (v.memory);
        for (int c = 0; c < impl_->inputs; ++c)
            h32[(v.inPtrArr >> 2) + c] = v.inBufs + (std::uint32_t) impl_->maxBlockSize * ss * (std::uint32_t) c;
        for (int c = 0; c < impl_->outputs; ++c)
            h32[(v.outPtrArr >> 2) + c] = v.outBufs + (std::uint32_t) impl_->maxBlockSize * ss * (std::uint32_t) c;

#if KROMOSYNTH_WASM3
        if (auto res = m3_CallV (v.fInit, (std::int32_t) v.dspPtr,
                                          (std::int32_t) impl_->sampleRate))
        { errOut = juce::String ("init: ") + res; return false; }
#else
        wasmtime_val_raw_t args[2];
        args[0].i32 = (std::int32_t) v.dspPtr;
        args[1].i32 = (std::int32_t) impl_->sampleRate;
        if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fInit, args, 2, &trap))
        { wasmtime_error_delete (err); errOut = "init failed"; return false; }
        if (trap != nullptr) { wasm_trap_delete (trap); errOut = "init trapped"; return false; }
#endif
    }

    return true;
}

FaustEngine::LoadResult
FaustEngine::load (const juce::File& wasmFile,
                   const juce::File& jsonFile,
                   double sampleRate,
                   int maxBlockSize,
                   int numVoices)
{
    LoadResult r;
    loaded_ = false;

    if (! wasmFile.existsAsFile()) { r.detail = "no such .wasm: " + wasmFile.getFullPathName(); return r; }
    if (! jsonFile.existsAsFile()) { r.detail = "no such .json: " + jsonFile.getFullPathName(); return r; }

    juce::MemoryBlock mb;
    if (! wasmFile.loadFileAsData (mb)) { r.detail = "could not read the .wasm"; return r; }

    return loadFromMemory (mb.getData(), mb.getSize(), jsonFile.loadFileAsString(),
                           sampleRate, maxBlockSize, numVoices);
}

FaustEngine::LoadResult
FaustEngine::loadFromMemory (const void* wasmBytes,
                             size_t wasmSize,
                             const juce::String& dspJson,
                             double sampleRate,
                             int maxBlockSize,
                             int numVoices)
{
    LoadResult r;
    loaded_ = false;

    if (wasmBytes == nullptr || wasmSize == 0) { r.detail = "empty .wasm"; return r; }
    if (numVoices < 1 || maxBlockSize < 1) { r.detail = "voices and block size must be positive"; return r; }

    // ── metadata ────────────────────────────────────────────────────────────
    const auto parsed = juce::JSON::parse (dspJson);

    if (! parsed.isObject()) { r.detail = "DSP JSON did not parse as an object"; return r; }

    impl_->structBytes = (int) parsed.getProperty ("size", 0);
    impl_->inputs      = (int) parsed.getProperty ("inputs", 0);
    impl_->outputs     = (int) parsed.getProperty ("outputs", 0);
    impl_->dspName     = parsed.getProperty ("name", "").toString();

    if (impl_->structBytes <= 0) { r.detail = "DSP JSON has no usable `size`"; return r; }
    if (impl_->outputs <= 0)     { r.detail = "DSP JSON reports no outputs"; return r; }

    // Walk the ui tree for the polyphony parameters. Faust nests them in
    // vgroup/hgroup items, so this recurses rather than assuming a flat list.
    impl_->idxFreq = impl_->idxGain = impl_->idxGate = -1;
    impl_->params.clear();

    std::function<void (const juce::var&)> walk = [&] (const juce::var& node)
    {
        if (auto* arr = node.getArray())
        {
            for (const auto& item : *arr)
            {
                if (item.hasProperty ("items")) { walk (item.getProperty ("items", {})); continue; }

                const auto label = item.getProperty ("label", "").toString();
                const int index = (int) item.getProperty ("index", -1);

                if (index < 0) continue;

                Parameter p;
                p.label = label;
                p.byteOffset = index;
                p.isButton = item.getProperty ("type", "").toString() == "button";
                p.init = (float) (double) item.getProperty ("init", 0.0);
                p.minimum = (float) (double) item.getProperty ("min", 0.0);
                p.maximum = (float) (double) item.getProperty ("max", 1.0);
                impl_->params.push_back (p);

                if (label == "freq") impl_->idxFreq = index;
                else if (label == "gain") impl_->idxGain = index;
                else if (label == "gate") impl_->idxGate = index;
            }
        }
    };
    walk (parsed.getProperty ("ui", {}));

    r.hasFreq = impl_->idxFreq >= 0;
    r.hasGain = impl_->idxGain >= 0;
    r.hasGate = impl_->idxGate >= 0;

    // ── module bytes ────────────────────────────────────────────────────────
    impl_->wasm.assign ((const std::uint8_t*) wasmBytes,
                        (const std::uint8_t*) wasmBytes + wasmSize);

    impl_->sampleRate = sampleRate;
    impl_->maxBlockSize = maxBlockSize;
    impl_->mix.assign ((size_t) maxBlockSize, 0.0f);

    // A voice gets this long after note-off to show it is decaying before the
    // plugin stops it for them. 1.5 s is chosen so an exponential tail heading
    // for -60 dB at 8 s -- roughly the longest the sampler's SFZ artefacts
    // declare -- has already lost ~11 dB and is safely under the threshold.
    impl_->graceFrames = (int) (1.5 * sampleRate);
    impl_->fadeFrames  = (int) (0.2 * sampleRate);
    impl_->compRampStep = 1.0f / juce::jmax (1.0f, (float) (0.005 * sampleRate));

    // 1 ms attack, 150 ms release. Fast enough that the overshoot the clamp has
    // to catch is a sample or two; slow enough on release that a loud note does
    // not chatter the gain back up between cycles of its own waveform.
    impl_->limiterAttack = std::exp (-1.0f / (0.001f * (float) sampleRate));
    impl_->limiterRelease = std::exp (-1.0f / (0.150f * (float) sampleRate));
    impl_->limiterGain = 1.0f;

    // A USABLE default envelope, not a zeroed one. envReleaseCoef starting at
    // 0.0 would make every release instantaneous -- a hard cut, which is a
    // click -- for any caller that never sets the parameters, and faust-probe
    // is exactly such a caller. Flat while held, 1.5 s release, matching the
    // grace period the forced release already used.
    setEnvelope (0.0f, 0.0f, 1.0f, 1.5f);

#if KROMOSYNTH_WASM3
    // No engine, no shared module, no linker. The environment is the only
    // shared object; parsing, linking and memory all belong to the per-voice
    // runtimes and happen in instantiateVoices.
    if (impl_->env == nullptr) impl_->env = m3_NewEnvironment();
    if (impl_->env == nullptr) { r.detail = "m3_NewEnvironment failed"; return r; }

    for (auto& v : impl_->voices)
        if (v.rt != nullptr) m3_FreeRuntime (v.rt);
    impl_->voices.clear();
#else
    if (impl_->engine == nullptr) impl_->engine = wasm_engine_new();
    if (impl_->engine == nullptr) { r.detail = "wasm_engine_new failed"; return r; }

    for (auto& v : impl_->voices)
        if (v.store != nullptr) wasmtime_store_delete (v.store);
    impl_->voices.clear();

    if (impl_->linker != nullptr) { wasmtime_linker_delete (impl_->linker); impl_->linker = nullptr; }
    if (impl_->module != nullptr) { wasmtime_module_delete (impl_->module); impl_->module = nullptr; }

    if (auto* err = wasmtime_module_new (impl_->engine, impl_->wasm.data(),
                                         impl_->wasm.size(), &impl_->module))
    {
        wasmtime_error_delete (err);
        r.detail = "wasmtime_module_new failed (Cranelift could not compile the module)";
        return r;
    }

    impl_->linker = wasmtime_linker_new (impl_->engine);

    // The FULL table is linked, not the measured union -- see the header. With
    // wasmtime, unlike wasm3, defining a name the module does not import is
    // simply unused rather than an error, so there is nothing to tolerate here.
    for (const auto& link : kMathTable)
    {
        wasm_functype_t* ty = funcType (link.arity);
        auto* err = wasmtime_linker_define_func_unchecked (
            impl_->linker, "env", 3, link.name, std::strlen (link.name),
            ty, link.fn, nullptr, nullptr);
        wasm_functype_delete (ty);

        if (err != nullptr)
        {
            wasmtime_error_delete (err);
            r.detail = juce::String ("could not define import env.") + link.name;
            return r;
        }
    }
#endif

    if (! instantiateVoices (numVoices, r.detail))
        return r;

    loaded_ = true;
    peak_.store (0.0f, std::memory_order_relaxed);
    blocks_.store (0, std::memory_order_relaxed);

    r.ok = true;
    r.structBytes = impl_->structBytes;
    r.outputs = impl_->outputs;
    r.voices = numVoices;
    r.dspName = impl_->dspName;
    r.detail = "loaded";
    return r;
}

void FaustEngine::reset() noexcept
{
    if (! loaded_) return;

    for (auto& v : impl_->voices)
    {
        v.note = -1;
        v.gateOpen = false;
        if (impl_->idxGate >= 0) setParam (v, impl_->idxGate, 0.0f);
    }
    peak_.store (0.0f, std::memory_order_relaxed);
}

void FaustEngine::setParam (FaustVoice& v, int byteOffset, float value) noexcept
{
#if KROMOSYNTH_WASM3
    m3_CallV (v.fSetParam, (std::int32_t) v.dspPtr, (std::int32_t) byteOffset, value);
#else
    wasmtime_val_raw_t args[3];
    args[0].i32 = (std::int32_t) v.dspPtr;
    args[1].i32 = (std::int32_t) byteOffset;
    args[2].f32 = value;

    wasm_trap_t* trap = nullptr;
    if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fSetParam, args, 3, &trap))
        wasmtime_error_delete (err);
    if (trap != nullptr) wasm_trap_delete (trap);
#endif
}

void FaustEngine::setOutputGain (float g) noexcept
{
    if (impl_ != nullptr) impl_->outputGain = g;
}

float FaustEngine::outputGain() const noexcept
{
    return impl_ == nullptr ? 1.0f : impl_->outputGain;
}

bool FaustEngine::setVoiceCount (int numVoices)
{
    if (! loaded_ || numVoices < 1) return false;

    juce::String err;
    if (! instantiateVoices (numVoices, err)) { loaded_ = false; return false; }

    impl_->sustainDown = false;
    return true;
}

int FaustEngine::capForRtf (double rtf) noexcept
{
    // rtf / (v * 1.05) >= 4.0 is exactly v <= rtf / 4.2, so no search is needed.
    // rtf is non-negative, and rtf < 4.2 floors to 0 which jlimit returns as 1 --
    // the same answer the old power-of-two loop's initialiser gave.
    return juce::jlimit (1, 32, (int) std::floor (rtf / 4.2));
}

juce::String FaustEngine::runtimeId()
{
    // Composed from the linked runtime's own version macro, so an upgrade
    // changes the id without anyone remembering to edit it. The OS and arch
    // are part of it because the same wasmtime on a different target is a
    // different measurement.
   #if JUCE_IOS
    const char* os = "ios";
   #elif JUCE_MAC
    const char* os = "macos";
   #elif JUCE_WINDOWS
    const char* os = "windows";
   #elif JUCE_LINUX
    const char* os = "linux";
   #else
    const char* os = "unknown";
   #endif

   #if defined (__aarch64__) || defined (_M_ARM64)
    const char* arch = "arm64";
   #elif defined (__x86_64__) || defined (_M_X64)
    const char* arch = "x86_64";
   #else
    const char* arch = "unknown";
   #endif

#if KROMOSYNTH_WASM3
    // The runtime identity MUST change with the runtime. ARTEFACT_CONTRACT §3.2
    // gates an artefact's stored `rtf` on rtfPlatform matching this string, and
    // an rtf measured under Cranelift means nothing under an interpreter --
    // wasm3 runs 5.56x slower at the median, so a voice cap derived from a
    // wasmtime figure would be roughly five times too generous.
    //
    // The PEAKS are a different matter: wasm3 is bit-identical to wasmtime on
    // all 120 archive elites, so peak, trim and articulation transfer intact.
    // Splitting those two trust rules is item B2 in NEXT_SESSION_PROMPT.md; the
    // contract currently gates both on this one string because a single render
    // produced both numbers.
    return juce::String ("wasm3-")
         + juce::String (M3_VERSION_MAJOR) + "."
         + juce::String (M3_VERSION_MINOR) + "."
         + juce::String (M3_VERSION_REV)
         + "-" + os + "-" + arch;
#else
    return juce::String ("wasmtime-") + WASMTIME_VERSION + "-" + os + "-" + arch;
#endif
}

FaustEngine::Measured FaustEngine::measure()
{
    Measured m;
    if (! loaded_) return m;

    int idxFreq = -1, idxGain = -1, idxGate = -1;
    for (const auto& prm : impl_->params)
    {
        if (prm.label == "freq") idxFreq = prm.byteOffset;
        if (prm.label == "gain") idxGain = prm.byteOffset;
        if (prm.label == "gate") idxGate = prm.byteOffset;
    }

    const int pitches[] = { 48, 69, 81 };                     // c3, a4, a5

    // THE SAME WINDOW faust-probe's runMeasure uses, and the same one
    // render-socket's port uses: 2 s holding the gate, then 3 s after closing
    // it, with the peak taken across both.
    //
    // This was 1.5 s of hold and no release phase at all, which made it a third
    // implementation of one measurement that disagreed with the other two. On
    // 01M1JFPEPBR4NFC83AEMA7TA1Y, same module and same 44100/1024, the probe
    // read 4.4948 and this read 3.8008 -- 1.46 dB low, because the genome had
    // not reached its peak inside 1.5 s.
    //
    // Low is the DANGEROUS direction: the trim comes out too high and the
    // instrument plays LOUDER than intended, on a genome whose raw peak is
    // already 3.8. Caught by the artefact cross-check on its first outing --
    // which is what that check is for.
    //
    // The cost is real: 15 s of audio per load instead of 4.5. On the slowest
    // genome measured (rtf 21) that moves the load-time measurement from ~210
    // ms to ~700 ms. Paid, because a wrong level is audible and a slower load
    // is not.
    const int holdBlocks  = (int) (2.0 * impl_->sampleRate) / impl_->maxBlockSize;
    const int afterBlocks = (int) (3.0 * impl_->sampleRate) / impl_->maxBlockSize;

    juce::AudioBuffer<float> scratch { 1, impl_->maxBlockSize };
    const juce::MidiBuffer noMidi;

    // Measured with the trim OFF, or the second measurement would be of the
    // first one's result.
    const float saved = impl_->outputGain;
    impl_->outputGain = 1.0f;

    // And with the ceiling off, for the same reason one step further: a genome
    // peaking at 11.39 would be reported as peaking at 1.0, and every level
    // decision downstream would be made from a number the ceiling invented.
    const bool savedCeiling = impl_->ceiling;
    impl_->ceiling = false;

    // Likewise the user's output level: a measurement taken through it would
    // change every time they moved a fader.
    const float savedLevel = impl_->outputLevel.load (std::memory_order_relaxed);
    impl_->outputLevel.store (1.0f, std::memory_order_relaxed);

    float peak = 0.0f;
    float perPitch[3] = { 0.0f, 0.0f, 0.0f };
    const auto t0 = juce::Time::getHighResolutionTicks();

    // Samples from the a4 probe, kept for the spectrogram. a4 rather than all
    // three: three pitches concatenated would read as three unrelated events,
    // and a4 is the pitch that decided the peak for most genomes measured.
    // Reserved once so the capture never reallocates mid-render and perturbs
    // the rtf timing that shares this loop.
    std::vector<float> capture;
    capture.reserve ((size_t) ((2.0 + 3.0) * impl_->sampleRate) + (size_t) impl_->maxBlockSize);

    for (int pi = 0; pi < (int) (sizeof (pitches) / sizeof (pitches[0])); ++pi)
    {
        const int p = pitches[pi];
        const bool capturing = (p == 69);
        float here = 0.0f;

        panic();
        // Parameter-driven, so forced release never engages: this must measure
        // the genome, not the plugin's handling of it.
        if (idxFreq >= 0) setParameter (idxFreq, mtof (p));
        if (idxGain >= 0) setParameter (idxGain, 1.0f);   // full velocity, the worst case
        if (idxGate >= 0) setParameter (idxGate, 1.0f);

        for (int b = 0; b < holdBlocks; ++b)
        {
            process (scratch, noMidi);
            here = juce::jmax (here, scratch.getMagnitude (0, impl_->maxBlockSize));
            if (capturing)
                capture.insert (capture.end(), scratch.getReadPointer (0),
                                scratch.getReadPointer (0) + impl_->maxBlockSize);
        }

        // Closed by PARAMETER, not by noteOff: a note-driven release would
        // engage the forced-stop logic and measure the plugin rather than the
        // genome. Voices stay non-note-driven, so nothing idles underneath.
        if (idxGate >= 0) setParameter (idxGate, 0.0f);

        for (int b = 0; b < afterBlocks; ++b)
        {
            process (scratch, noMidi);
            here = juce::jmax (here, scratch.getMagnitude (0, impl_->maxBlockSize));
            if (capturing)
                capture.insert (capture.end(), scratch.getReadPointer (0),
                                scratch.getReadPointer (0) + impl_->maxBlockSize);
        }

        // Kept PER PITCH, not just folded into the maximum. The maximum is what
        // a single output gain would be set from, and on a genome measured at
        // c3 11.39 / a4 0.40 that gain makes the instrument inaudible wherever
        // it was auditioned. These three are what setPitchCompensation needs.
        perPitch[pi] = here;
        peak = juce::jmax (peak, here);
    }

    const double elapsed = juce::Time::highResolutionTicksToSeconds (
                               juce::Time::getHighResolutionTicks() - t0);

    panic();
    impl_->outputGain = saved;
    impl_->ceiling = savedCeiling;
    impl_->outputLevel.store (savedLevel, std::memory_order_relaxed);
    peak_.store (0.0f, std::memory_order_relaxed);

    m.peak = peak;
    m.peakC3 = perPitch[0];
    m.peakA4 = perPitch[1];
    m.peakA5 = perPitch[2];

    // Free-running, and labelled so. d3321de measured that holding a real block
    // cadence costs 2.3-2.9x more than rendering flat out, so this ranks
    // genomes against each other; it is not a headroom claim about a busy DAW.
    // Whether >=4x is the right bar is what rt-sweep.mjs still has to answer.
    const double audioSeconds = 3 * (2.0 + 3.0);
    m.rtf = elapsed > 0.0 ? audioSeconds / elapsed : 0.0;

    // Gate 0 §5 validated single-voice RTF as an exact predictor of polyphony,
    // 30/30, with a 1.05 per-voice penalty.
    //
    // CONTINUOUS, not a power-of-two ladder. This used to step `v *= 2`, so it
    // only ever tested 1, 2, 4, 8, 16, 32 -- making the cap the largest TESTED
    // value that clears the bar rather than the largest value that clears it.
    // Measured on an iPhone 13 Pro Max, 2026-09-08: rtf 14.7 and 15.8 both
    // permit 3 voices and got 2; rtf 24.5 permits 5 and got 4. About a third of
    // the available polyphony, lost at the top of every band.
    //
    // It landed hardest exactly where it hurts most. A genome that sustains
    // after note-off never frees a voice, so with a cap of 2 it force-stopped 4
    // of 11 held notes -- and 48 of the 120 archive genomes are drones.
    //
    // The intent was already recorded in NEXT_SESSION_PROMPT.2026-09-01.md:
    // "the useful question is how many voices does each genome get on each
    // platform -- a CONTINUOUS answer where the rule wants a pass/fail", and
    // "derive the per-platform cap arithmetically". The ladder contradicted it.
    //
    // THE 4x BAR IS UNCHANGED, and deliberately. m.rtf above is measured
    // free-running, and d3321de measured that holding a real block cadence
    // costs 2.3-2.9x more, so 4x free-running is nearer 1.4-1.7x in a busy
    // host. That is not a margin to spend. Whether the bar itself is right
    // remains rt-sweep.mjs's question.
    //
    // rtf is non-negative and rtf < 4.2 floors to 0, which jlimit returns as 1
    // -- the same answer the old loop's initialiser gave.
    m.capAt4x = capForRtf (m.rtf);

    // Computed AFTER the timing is taken, so the FFT never lands inside the
    // measured interval and inflates the genome's apparent cost.
    m.spectrogram = buildSpectrogram (capture, impl_->sampleRate);

    // ── the pitch sweep ─────────────────────────────────────────────────────
    //
    // THE SAME WINDOW as the three reference pitches, and reusing their
    // measurements rather than repeating them.
    //
    // This used 1 s of hold and 0.5 s of tail, on the reasoning that only the
    // SHAPE of level against pitch was wanted and the shape is established
    // early. That reasoning is wrong, and measurably so. On
    // 01M1KY1AKTTHBPF9Z9KP9BRN6E (tt_supersaw_lead) the short window read
    // 15.3 dB low at midi 60 and 14.5 dB low at midi 72, and on
    // 01M1JD666ZTWGTCT4W2GDTVQMF it read 26.1 dB low at midi 48 -- unevenly,
    // so no single scale factor could correct it.
    //
    // Under-measuring makes the correction TOO LARGE, so those pitches were
    // boosted rather than attenuated and delivered up to 3.9x full scale. That
    // is a genome which is delicate in the preview and loud and clipping in
    // the plugin, which is exactly how it was reported.
    //
    // A 2 s hold with no tail would be right for 9 of 12 pitches measured and
    // up to 4 dB low for the rest. Not good enough for a number the whole
    // keyboard's level is derived from.
    //
    // The cost is nine more full-window renders, 45 s of audio: about 75 ms on
    // a fast genome and a little over two seconds on the slowest in the
    // archive. measure_ms is logged, so it is visible rather than mysterious.
    {
        m.pitchPeaks.resize ((size_t) kSweepCount, 0.0f);

        for (int s = 0; s < kSweepCount; ++s)
        {
            const int note = kSweepFirstNote + s * kSweepStep;

            // c3, a4 and a5 were measured above at this exact window. Rendering
            // them again would cost 15 s to reproduce numbers already held.
            if (note == 48) { m.pitchPeaks[(size_t) s] = perPitch[0]; continue; }
            if (note == 69) { m.pitchPeaks[(size_t) s] = perPitch[1]; continue; }
            if (note == 81) { m.pitchPeaks[(size_t) s] = perPitch[2]; continue; }

            panic();
            if (idxFreq >= 0) setParameter (idxFreq, mtof (note));
            if (idxGain >= 0) setParameter (idxGain, 1.0f);
            if (idxGate >= 0) setParameter (idxGate, 1.0f);

            float here = 0.0f;
            for (int b = 0; b < holdBlocks; ++b)
            {
                process (scratch, noMidi);
                here = juce::jmax (here, scratch.getMagnitude (0, impl_->maxBlockSize));
            }

            if (idxGate >= 0) setParameter (idxGate, 0.0f);

            for (int b = 0; b < afterBlocks; ++b)
            {
                process (scratch, noMidi);
                here = juce::jmax (here, scratch.getMagnitude (0, impl_->maxBlockSize));
            }

            m.pitchPeaks[(size_t) s] = here;
        }

        panic();
        peak_.store (0.0f, std::memory_order_relaxed);
    }

    return m;
}

int FaustEngine::forcedStops() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->forcedStops.load (std::memory_order_relaxed);
}

void FaustEngine::panic() noexcept
{
    if (! loaded_) return;

    for (auto& v : impl_->voices)
    {
        v.note = -1;
        v.gateOpen = false;
        v.releasing = false;
        v.forcing = false;
        v.forceGain = 1.0f;
        v.idle = true;
        v.quietBlocks = 0;
        v.sustained = false;
        v.compGain = 1.0f;
        v.compTarget = 1.0f;
        v.retriggering = false;
        v.pendingNote = -1;
        v.envStage = FaustVoice::Env::off;
        v.envLevel = 0.0f;

        // init() rather than gate=0. A genome that ignores its gate ignores it
        // during a panic too, and 16 of 33 measured elites do exactly that.
        // Re-initialising zeroes the DSP struct, which no genome can decline.
#if KROMOSYNTH_WASM3
        m3_CallV (v.fInit, (std::int32_t) v.dspPtr, (std::int32_t) impl_->sampleRate);
#else
        wasmtime_val_raw_t args[2];
        args[0].i32 = (std::int32_t) v.dspPtr;
        args[1].i32 = (std::int32_t) impl_->sampleRate;

        wasm_trap_t* trap = nullptr;
        if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fInit, args, 2, &trap))
            wasmtime_error_delete (err);
        if (trap != nullptr) wasm_trap_delete (trap);
#endif
    }

    peak_.store (0.0f, std::memory_order_relaxed);
}

const std::vector<FaustEngine::Parameter>& FaustEngine::parameters() const noexcept
{
    return impl_->params;
}

void FaustEngine::setParameter (int byteOffset, float value) noexcept
{
    if (! loaded_) return;

    for (auto& v : impl_->voices)
    {
        setParam (v, byteOffset, value);

        // Setting a parameter is an instruction to sound, so it wakes the
        // voice. Without this the null test went silent the moment idle voices
        // stopped being computed: it drives parameters directly and never
        // issues a note-on, so nothing would ever have left idle. Caught by
        // running it, not by reading.
        v.idle = false;
        v.quietBlocks = 0;
        v.noteDriven = false;
    }
}

void FaustEngine::writeParameter (int byteOffset, float value) noexcept
{
    if (! loaded_) return;

    // The whole of it. Everything setParameter does beyond this line is the
    // part a per-block caller must not have -- see the header.
    for (auto& v : impl_->voices)
        setParam (v, byteOffset, value);
}

void FaustEngine::setOutputCeiling (bool shouldLimit) noexcept
{
    if (impl_ != nullptr) impl_->ceiling = shouldLimit;
}

void FaustEngine::setOutputLevel (float linearGain) noexcept
{
    if (impl_ != nullptr)
        impl_->outputLevel.store (juce::jlimit (0.0f, 4.0f, linearGain),
                                  std::memory_order_relaxed);
}

void FaustEngine::setEnvelope (float attackS, float decayS,
                               float sustain, float releaseS) noexcept
{
    if (impl_ == nullptr) return;

    const float sr = (float) juce::jmax (1.0, impl_->sampleRate);

    // A zero-length stage becomes a one-sample stage rather than a division by
    // zero, so "instant" is expressible and still ramped.
    impl_->envAttackInc.store (1.0f / juce::jmax (1.0f, attackS * sr),
                               std::memory_order_relaxed);
    impl_->envDecayInc.store (1.0f / juce::jmax (1.0f, decayS * sr),
                              std::memory_order_relaxed);
    impl_->envSustain.store (juce::jlimit (0.0f, 1.0f, sustain),
                             std::memory_order_relaxed);

    // Release is EXPONENTIAL where attack and decay are linear, because a
    // linear fade to zero has an audible corner at the end and a decaying one
    // does not. The coefficient is set so the level reaches -60 dB in the
    // requested time, which is the usual reading of a release length.
    impl_->envReleaseCoef.store (
        std::exp (-6.9078f / juce::jmax (1.0f, releaseS * sr)),
        std::memory_order_relaxed);
}

void FaustEngine::setPitchCompensation (const std::vector<float>& pitchPeaks, float referencePeak)
{
    if (impl_ == nullptr) return;

    impl_->pitchPeaks = pitchPeaks;
    impl_->referencePeak = referencePeak;
}

namespace
{
/** Estimated peak at a MIDI note, read from the measured sweep.

    Interpolated in dB between adjacent entries, because level tracks pitch
    multiplicatively. Clamped at both ends rather than extrapolated: the table
    says nothing about pitches outside the range it was measured over, and an
    extrapolation is most confident exactly where it has no evidence.
*/
float estimatePeakAt (int note, const std::vector<float>& table) noexcept
{
    if (table.empty()) return 0.0f;

    const float pos = (float) (note - FaustEngine::kSweepFirstNote)
                    / (float) FaustEngine::kSweepStep;

    if (pos <= 0.0f) return table.front();
    if (pos >= (float) (table.size() - 1)) return table.back();

    const int i = (int) pos;
    const float frac = pos - (float) i;

    const float a = std::log10 (juce::jmax (1.0e-6f, table[(size_t) i]));
    const float b = std::log10 (juce::jmax (1.0e-6f, table[(size_t) i + 1]));

    return std::pow (10.0f, a + (b - a) * frac);
}
} // namespace

void FaustEngine::noteOn (int note, int velocity) noexcept
{
    FaustVoice* chosen = nullptr;

    // SILENT VOICES FIRST, and this order is the whole fix for retrigger
    // crackle.
    //
    // This used to take the first voice with note < 0. noteOff sets note = -1
    // immediately while the voice is STILL RINGING -- a drone never decays at
    // all, and anything else has 1.5 s of grace before the forced release --
    // so after one note, voice 0 was "free" and still sounding, and every
    // subsequent note retriggered on top of it while voices 1..N sat idle and
    // already zeroed. Playing repeatedly hammered one voice.
    //
    // An idle voice has been through initVoice(), so its DSP struct is zeroed
    // and a note starting on it is indistinguishable from the first note after
    // a load -- which is what the browser preview does every time, and why it
    // has no crackle to begin with.
    for (auto& v : impl_->voices)
        if (v.idle) { chosen = &v; break; }

    // WHERE THE VOICE CAME FROM, counted.
    //
    // The three branches below are the allocator's whole policy, and until now
    // nothing reported which one fired. That is the gap that let a polyphony
    // regression ship on 2026-09-05 with the null test passing 33/33: the
    // instrument became audibly constrained under an arpeggio because released
    // voices stopped falling silent, and every tool available compared SAMPLES.
    // None of them could answer "did this note find a free voice, or did it
    // have to take one that was still sounding".
    //
    // Counted here rather than derived outside, because from outside a reuse
    // and a steal are indistinguishable -- both show a voice count that did not
    // rise.
    enum class Source { idle, released, stolen };
    Source source = Source::idle;

    // Then a released voice that has not yet fallen silent. Oldest first, so
    // the one that has had longest to decay is taken rather than whichever
    // happens to sit earliest in the array.
    if (chosen == nullptr)
    {
        std::uint64_t oldest = ~0ull;
        for (auto& v : impl_->voices)
            if (v.note < 0 && v.startedAt < oldest) { oldest = v.startedAt; chosen = &v; }

        if (chosen != nullptr) source = Source::released;
    }

    // Steal the oldest. Crude, and deliberately so until there is a reason to
    // be cleverer -- a released voice still ringing is indistinguishable here
    // from a held one, because nothing reads the module's envelope state.
    if (chosen == nullptr)
    {
        std::uint64_t oldest = ~0ull;
        for (auto& v : impl_->voices)
            if (v.startedAt < oldest) { oldest = v.startedAt; chosen = &v; }

        if (chosen != nullptr) source = Source::stolen;
    }

    if (chosen == nullptr) return;

    // Counted at ALLOCATION, before the retrigger fade below defers the start.
    // A note that has to wait for a fade still took the voice it took.
    switch (source)
    {
        case Source::idle:     impl_->startsIdle.fetch_add (1, std::memory_order_relaxed); break;
        case Source::released: impl_->startsReused.fetch_add (1, std::memory_order_relaxed); break;
        case Source::stolen:   impl_->startsStolen.fetch_add (1, std::memory_order_relaxed); break;
    }

    // A voice that is STILL SOUNDING is faded out before it is reused.
    //
    // Retriggering one on top of itself is a discontinuity: the frequency jumps
    // and the gate pulses while the previous note is still ringing. Measured
    // with faust-probe --clicks that is a boundary jump of 1.0 to 1.2, against
    // 0.0002 for a note starting from silence.
    //
    // It only happens under voice pressure -- the allocator prefers a silent
    // voice, then the oldest released one, and steals only when every voice is
    // busy -- but caps are often 4 to 8 and a drone never decays, so ordinary
    // playing reaches it.
    //
    // peakRecent is the test rather than `idle`, because a released voice can
    // be inaudible long before the idle machinery notices.
    if (! chosen->idle && chosen->peakRecent > 1.0e-4f)
    {
        chosen->pendingNote = note;
        chosen->pendingVelocity = velocity;
        chosen->retriggering = true;

        // The existing forced-release machinery does the fade; only its LENGTH
        // differs. A force-stop takes 0.2 s because it is ending a note that
        // will not end itself. This takes 2 ms, because a note is waiting.
        chosen->forcing = true;
        chosen->forceGain = 1.0f;
        return;
    }

    startNote (*chosen, note, velocity);
}

void FaustEngine::startNote (FaustVoice& v, int note, int velocity) noexcept
{
    auto* chosen = &v;

    // Captured BEFORE idle is cleared. A silent voice can take its correction
    // immediately -- there is nothing to step -- and that leaves the genome's
    // own attack exactly as it renders. Only a voice that is still sounding
    // needs the ramp.
    const bool wasSilent = chosen->idle;

    chosen->note = note;
    chosen->gateOpen = true;
    chosen->startedAt = ++impl_->counter;
    chosen->releasing = false;
    chosen->forcing = false;
    chosen->forceGain = 1.0f;
    chosen->framesSinceRelease = 0;
    chosen->idle = false;
    chosen->quietBlocks = 0;
    chosen->noteDriven = true;
    chosen->sustained = false;

    // Level this note to the reference pitch. Off when nothing was measured, so
    // the null test and any un-measured load behave exactly as before.
    if (! impl_->pitchPeaks.empty() && impl_->referencePeak > 0.0f)
    {
        const float here = estimatePeakAt (note, impl_->pitchPeaks);

        // ATTENUATE ONLY, and this is the load-bearing rule.
        //
        // A correction may pull a pitch DOWN towards the reference, which is
        // bounded and safe: the worst it can do is fail to attenuate enough.
        // It must never push one UP, because a boost is only justified where
        // the estimate is right, and an estimate is exactly what may be wrong
        // -- between table entries, or outside the measured range.
        //
        // Allowing up to 8x boost delivered 4.1 at midi 36 on
        // 01M1KY1AKTTHBPF9Z9KP9BRN6E, from an estimate clamped to the edge of a
        // table that did not reach that far. Measured over 10 archive genomes
        // at every semitone, allowing boost gives a worst delivered peak of
        // 3.810; forbidding it gives 1.909, with 9 of the 10 never exceeding
        // full scale at all.
        //
        // The cost is fidelity in one direction only: a pitch quieter than the
        // reference stays quieter, which is what the genome does and what the
        // preview would render.
        chosen->compTarget = here > 1.0e-6f
            ? juce::jmin (1.0f, impl_->referencePeak / here)
            : 1.0f;
    }
    else
    {
        chosen->compTarget = 1.0f;
    }

    if (wasSilent)
        chosen->compGain = chosen->compTarget;

    // Attack starts from wherever the envelope IS, not from zero. On a silent
    // voice that is zero anyway; on one still releasing it avoids a step down
    // to zero before the rise, which would be a click of exactly the kind the
    // allocator was changed to avoid.
    chosen->envStage = FaustVoice::Env::attack;

    impl_->notesOn.fetch_add (1, std::memory_order_relaxed);

    if (impl_->idxFreq >= 0) setParam (*chosen, impl_->idxFreq, mtof (note));
    if (impl_->idxGain >= 0) setParam (*chosen, impl_->idxGain, (float) velocity / 127.0f);
    if (impl_->idxGate >= 0)
    {
        // Retriggering a stolen voice needs the gate to fall before it rises,
        // or a Faust envelope on a rising edge never sees one.
        setParam (*chosen, impl_->idxGate, 0.0f);
        setParam (*chosen, impl_->idxGate, 1.0f);
    }
}

void FaustEngine::noteOff (int note) noexcept
{
    impl_->notesOff.fetch_add (1, std::memory_order_relaxed);

    // A note released before it ever started.
    //
    // A stolen voice holds the OLD note number while it fades, with the new one
    // waiting in pendingNote. A note-off arriving inside that window -- up to a
    // block, 23 ms at 1024 -- matches nothing, and the note would start after
    // the fade with no key still holding it: a voice sounding forever with no
    // way to release it.
    //
    // Found by faust-probe --clicks flagging a click at note-OFF, on a change
    // that only touched note-ON. The click was the symptom; a stuck note was
    // the fault.
    for (auto& v : impl_->voices)
    {
        if (v.retriggering && v.pendingNote == note)
        {
            v.retriggering = false;
            v.pendingNote = -1;
            return;
        }
    }

    for (auto& v : impl_->voices)
    {
        if (v.note != note || ! v.gateOpen) continue;

        // Pedal down: the key is up but the note is not. Cleared so the same
        // key retriggers on a fresh voice, which is what a player expects.
        if (impl_->sustainDown)
        {
            v.sustained = true;
            v.note = -1;
            continue;
        }

        v.gateOpen = false;
        if (impl_->idxGate >= 0) setParam (v, impl_->idxGate, 0.0f);

        // Nothing in the wasm ABI reports when a Faust envelope has finished,
        // so the voice is watched rather than trusted. note goes to -1 so the
        // same key can retrigger immediately; `releasing` keeps the decay
        // measurement running underneath that.
        v.note = -1;
        v.releasing = true;
        v.framesSinceRelease = 0;
        v.envStage = FaustVoice::Env::release;
        v.peakAtRelease = v.peakRecent;
        v.forcing = false;
        v.forceGain = 1.0f;
    }
}

void FaustEngine::setSustain (bool down) noexcept
{
    if (! loaded_ || impl_->sustainDown == down) return;

    impl_->sustainDown = down;
    impl_->sustainEvents.fetch_add (1, std::memory_order_relaxed);

    if (down) return;

    for (auto& v : impl_->voices)
    {
        if (! v.sustained) continue;

        v.sustained = false;
        v.gateOpen = false;
        if (impl_->idxGate >= 0) setParam (v, impl_->idxGate, 0.0f);

        v.releasing = true;
        v.framesSinceRelease = 0;
        v.peakAtRelease = v.peakRecent;
        v.forcing = false;
        v.forceGain = 1.0f;
    }
}

int FaustEngine::notesOn() const noexcept   { return impl_->notesOn.load (std::memory_order_relaxed); }
int FaustEngine::notesOff() const noexcept  { return impl_->notesOff.load (std::memory_order_relaxed); }

FaustEngine::VoiceStarts FaustEngine::voiceStarts() const noexcept
{
    VoiceStarts s;
    if (impl_ == nullptr) return s;

    s.idle   = impl_->startsIdle.load (std::memory_order_relaxed);
    s.reused = impl_->startsReused.load (std::memory_order_relaxed);
    s.stolen = impl_->startsStolen.load (std::memory_order_relaxed);
    return s;
}

int FaustEngine::activeVoices() const noexcept
{
    if (! loaded_) return 0;
    int n = 0;
    for (const auto& v : impl_->voices) if (! v.idle) ++n;
    return n;
}

void FaustEngine::process (juce::AudioBuffer<float>& buffer,
                           const juce::MidiBuffer& midi) noexcept
{
    const int numFrames = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (! loaded_ || numFrames <= 0 || numChannels <= 0) { buffer.clear(); return; }

    buffer.clear();

    auto event = midi.cbegin();
    const auto lastEvent = midi.cend();

    int pos = 0;

    while (pos < numFrames)
    {
        // Chunk at the next MIDI event so note timing lands within a block
        // rather than at its edge, and never exceed the block size the layout
        // was built for.
        int chunkEnd = juce::jmin (numFrames, pos + impl_->maxBlockSize);

        for (auto it = event; it != lastEvent; ++it)
        {
            const auto samplePos = (*it).samplePosition;
            if (samplePos <= pos) continue;
            if (samplePos < chunkEnd) chunkEnd = samplePos;
            break;
        }

        while (event != lastEvent && (*event).samplePosition <= pos)
        {
            const auto message = (*event).getMessage();

            if (message.isNoteOn())        noteOn (message.getNoteNumber(), message.getVelocity());
            else if (message.isNoteOff())  noteOff (message.getNoteNumber());
            else if (message.isSustainPedalOn())  setSustain (true);
            else if (message.isSustainPedalOff()) setSustain (false);
            else if (message.isAllNotesOff() || message.isAllSoundOff()) panic();

            ++event;
        }

        const int chunk = chunkEnd - pos;
        if (chunk <= 0) break;

        std::fill (impl_->mix.begin(), impl_->mix.begin() + chunk, 0.0f);

        for (auto& v : impl_->voices)
        {
            if (v.idle) continue;

#if KROMOSYNTH_WASM3
            // m3_CallV returns a message on trap and nullptr otherwise, so the
            // two wasmtime failure paths collapse into one here. `continue`
            // leaves this voice silent for the block rather than aborting the
            // whole render, which is what the wasmtime branch does too.
            if (m3_CallV (v.fCompute, (std::int32_t) v.dspPtr, chunk,
                                      (std::int32_t) v.inPtrArr,
                                      (std::int32_t) v.outPtrArr) != nullptr)
                continue;
#else
            wasmtime_val_raw_t args[4];
            args[0].i32 = (std::int32_t) v.dspPtr;
            args[1].i32 = chunk;
            args[2].i32 = (std::int32_t) v.inPtrArr;
            args[3].i32 = (std::int32_t) v.outPtrArr;

            wasm_trap_t* trap = nullptr;
            auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fCompute, args, 4, &trap);

            if (err != nullptr) { wasmtime_error_delete (err); continue; }
            if (trap != nullptr) { wasm_trap_delete (trap); continue; }
#endif

            const auto outBase = reinterpret_cast<std::uint32_t*> (v.memory)[v.outPtrArr >> 2];
            const auto* src = reinterpret_cast<const float*> (v.memory + outBase);

            float blockPeak = 0.0f;
            for (int i = 0; i < chunk; ++i)
                blockPeak = juce::jmax (blockPeak, std::abs (src[i]));
            v.peakRecent = blockPeak;

            if (v.releasing)
            {
                v.framesSinceRelease += chunk;

                // The decision, once, at the end of the grace period: has this
                // voice lost at least 6 dB since the note came off? A decaying
                // tail has; a drone has not. Anything that has not is never
                // going to stop, so the plugin stops it.
                if (! v.forcing && v.framesSinceRelease >= impl_->graceFrames)
                {
                    const bool decaying = v.peakAtRelease <= 0.0f
                                       || blockPeak < 0.5f * v.peakAtRelease;

                    if (decaying)
                    {
                        // RE-ARMED rather than disarmed. Deciding once would
                        // let a genome that loses 6 dB and then plateaus run
                        // forever, having passed the only test it was given. A
                        // genuine tail keeps clearing the bar every window; a
                        // plateau fails the next one.
                        v.framesSinceRelease = 0;
                        v.peakAtRelease = blockPeak;
                    }
                    else
                    {
                        v.forcing = true;
                        impl_->forcedStops.fetch_add (1, std::memory_order_relaxed);
                    }
                }
            }

            // Applied to every NOTE-DRIVEN voice, whatever stage it is in --
            // including Env::off, which means the release has finished and the
            // level is 0.
            //
            // Testing `stage != off` here instead was a bug with a nasty shape:
            // the moment a release completed the envelope stopped applying, so
            // the voice jumped back to full gain and the note came back. It
            // never reached a build because faust-probe --clicks showed sound
            // after a release that should have been silent.
            //
            // measure() drives the DSP through setParameter and never calls
            // noteOn, so its voices are not note-driven and are not shaped --
            // the same guard that keeps the pitch correction out of the
            // measurement.
            const bool enveloped = v.noteDriven;

            const float aInc = impl_->envAttackInc.load (std::memory_order_relaxed);
            const float dInc = impl_->envDecayInc.load (std::memory_order_relaxed);
            const float sLvl = impl_->envSustain.load (std::memory_order_relaxed);
            const float rCoef = impl_->envReleaseCoef.load (std::memory_order_relaxed);

            const auto stepEnvelope = [&] (FaustVoice& voice)
            {
                switch (voice.envStage)
                {
                    case FaustVoice::Env::attack:
                        voice.envLevel += aInc;
                        if (voice.envLevel >= 1.0f)
                        {
                            voice.envLevel = 1.0f;
                            voice.envStage = FaustVoice::Env::decay;
                        }
                        break;

                    case FaustVoice::Env::decay:
                        voice.envLevel -= dInc;
                        if (voice.envLevel <= sLvl)
                        {
                            voice.envLevel = sLvl;
                            voice.envStage = FaustVoice::Env::sustain;
                        }
                        break;

                    case FaustVoice::Env::sustain:
                        voice.envLevel = sLvl;
                        break;

                    case FaustVoice::Env::release:
                        voice.envLevel *= rCoef;
                        // Below -80 dB it is silence, and letting it crawl
                        // toward zero forever would keep the voice out of the
                        // idle path that zeroes its DSP state.
                        if (voice.envLevel < 1.0e-4f)
                        {
                            voice.envLevel = 0.0f;
                            voice.envStage = FaustVoice::Env::off;
                        }
                        break;

                    case FaustVoice::Env::off:
                        break;
                }

                return voice.envLevel;
            };

            if (v.forcing)
            {
                // TWO different fades through one mechanism. A force-stop is
                // ending a note that will not end itself, so 0.2 s is gentle. A
                // retrigger has a note waiting behind it, so 2 ms -- long
                // enough not to click, short enough not to be heard as delay.
                const int frames = v.retriggering
                    ? juce::jmax (1, (int) (0.002 * impl_->sampleRate))
                    : juce::jmax (1, impl_->fadeFrames);

                const float step = 1.0f / (float) frames;

                for (int i = 0; i < chunk; ++i)
                {
                    v.compGain = rampToward (v.compGain, v.compTarget, impl_->compRampStep);
                    const float env = enveloped ? stepEnvelope (v) : 1.0f;
                    impl_->mix[(size_t) i] += src[i] * v.forceGain * v.compGain * env;
                    v.forceGain = juce::jmax (0.0f, v.forceGain - step);
                }

                if (v.forceGain <= 0.0f)
                {
                    // Faded to nothing, so re-initialising cannot click. This
                    // is the only thing a genome cannot decline: init() zeroes
                    // the DSP struct.
                    v.forcing = false;
                    v.releasing = false;
                    v.peakRecent = 0.0f;
                    v.idle = true;

                    initVoice (v, impl_->sampleRate);

                    // A note was waiting for this fade. Start it now, from a
                    // zeroed struct and silence -- which is exactly the state a
                    // first note sees, and why this removes the click rather
                    // than moving it.
                    //
                    // The note begins at the NEXT chunk, so it is late by
                    // whatever remains of this one: up to a block, 23 ms at
                    // 1024 and 44100. Only under voice pressure, and the
                    // alternative was an audible click on every stolen voice.
                    if (v.retriggering && v.pendingNote >= 0)
                    {
                        const int note = v.pendingNote;
                        const int velocity = v.pendingVelocity;

                        v.retriggering = false;
                        v.pendingNote = -1;

                        startNote (v, note, velocity);
                    }
                }
            }
            else
            {
                for (int i = 0; i < chunk; ++i)
                {
                    v.compGain = rampToward (v.compGain, v.compTarget, impl_->compRampStep);
                    const float env = enveloped ? stepEnvelope (v) : 1.0f;
                    impl_->mix[(size_t) i] += src[i] * v.compGain * env;
                }
            }

            // A released voice that has actually fallen silent stops being
            // computed. Eight voices rendering inaudible tails forever is real
            // CPU, and it is the cost every RTF figure in EXPERIMENT_01 was
            // measured without.
            if (v.noteDriven && ! v.gateOpen && ! v.forcing && blockPeak < 1.0e-6f)
            {
                if (++v.quietBlocks > 8)
                {
                    v.idle = true;
                    v.releasing = false;

                    // ZEROED on the way out, not left as it was.
                    //
                    // Only the forced-stop path used to re-initialise, so a
                    // voice that simply went quiet kept the note's filter and
                    // delay state -- and the next note on that voice started on
                    // top of it. Measured with --clicks on
                    // 01M1JD666ZTWGTCT4W2GDTVQMF: playing the SAME note again
                    // produced a sample-to-sample jump of 1.66, against 0.04
                    // for the first note from a fresh engine. That is the
                    // crackle, and it is why the browser preview never has one
                    // -- it renders a single note into a fresh processor.
                    //
                    // Safe here specifically BECAUSE the voice is now idle. A
                    // genome that self-oscillates from a zeroed struct cannot
                    // start again while nothing calls compute() on it, which is
                    // the guarantee the idle flag already provides.
                    initVoice (v, impl_->sampleRate);
                }
            }
            else
            {
                v.quietBlocks = 0;
            }
        }

        // outputs == 1 for this substrate, so the mono result is copied across
        // the pair. See the header: this is the opposite of SamplerEngine.
        //
        // LIMITER before it goes out. Two things put the sum past full scale
        // even with per-note correction: voices ADD, and a resonance narrower
        // than the sweep's spacing can sit between two table entries. Measured
        // over the timbre-seeded run the residual reaches 23 dB, which is far
        // too much to shape sample by sample -- that is what turns a waveform
        // into a square. Reducing the gain of the whole signal instead sounds
        // like compression.
        //
        // softCeiling stays behind it, catching only the attack overshoot.
        const float level = impl_->outputLevel.load (std::memory_order_relaxed);

        for (int i = 0; i < chunk; ++i)
        {
            const float x = impl_->mix[(size_t) i] * impl_->outputGain * level;

            if (! impl_->ceiling) { impl_->mix[(size_t) i] = x; continue; }

            const float mag = std::abs (x);
            const float target = mag > kLimiterThreshold ? kLimiterThreshold / mag : 1.0f;

            // Down fast, up slowly. A one-pole either way, so the gain never
            // steps -- a stepped gain is itself a click.
            const float coef = target < impl_->limiterGain ? impl_->limiterAttack
                                                           : impl_->limiterRelease;
            impl_->limiterGain = target + (impl_->limiterGain - target) * coef;

            impl_->mix[(size_t) i] = softCeiling (x * impl_->limiterGain);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, pos, impl_->mix.data(), chunk, 1.0f);

        pos = chunkEnd;
    }

    // §4.2's guard. The failure this defends against is inaudible and passes
    // every validator, so the evidence has to be collected rather than assumed.
    const float blockPeak = buffer.getMagnitude (0, numFrames);
    float seen = peak_.load (std::memory_order_relaxed);
    while (blockPeak > seen && ! peak_.compare_exchange_weak (seen, blockPeak)) {}

    blocks_.fetch_add (1, std::memory_order_relaxed);
}
