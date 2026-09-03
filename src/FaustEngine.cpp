// FaustEngine.cpp — see the header for why each decision here is what it is.
//
// The wasm3 driving code is a port of probe/gate0/host_wasm3.c, which produced
// bit-identical output to the platform reference across the 120-elite archive.
// Divergence from that file should be treated as a bug in this one.

#include "FaustEngine.h"

#include <wasm.h>
#include <wasmtime.h>

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
#define UNARY(NAME, EXPR)                                                     \
    static wasm_trap_t* NAME (void*, wasmtime_caller_t*,                      \
                              wasmtime_val_raw_t* a, size_t) {                \
        const float x = a[0].f32; a[0].f32 = (float) (EXPR); return nullptr; }
#define BINARY(NAME, EXPR)                                                    \
    static wasm_trap_t* NAME (void*, wasmtime_caller_t*,                      \
                              wasmtime_val_raw_t* a, size_t) {                \
        const float x = a[0].f32, y = a[1].f32;                               \
        a[0].f32 = (float) (EXPR); return nullptr; }

UNARY (fe_abs,   std::fabs ((double) x))
UNARY (fe_acos,  std::acos ((double) x))
UNARY (fe_asin,  std::asin ((double) x))
UNARY (fe_atan,  std::atan ((double) x))
UNARY (fe_ceil,  std::ceil ((double) x))
UNARY (fe_cos,   std::cos ((double) x))
UNARY (fe_exp,   std::exp ((double) x))
UNARY (fe_floor, std::floor ((double) x))
UNARY (fe_log,   std::log ((double) x))
UNARY (fe_log10, std::log10 ((double) x))
UNARY (fe_round, std::round ((double) x))
UNARY (fe_sin,   std::sin ((double) x))
UNARY (fe_sqrt,  std::sqrt ((double) x))
UNARY (fe_tan,   std::tan ((double) x))
UNARY (fe_acosh, std::acosh ((double) x))
UNARY (fe_asinh, std::asinh ((double) x))
UNARY (fe_atanh, std::atanh ((double) x))
UNARY (fe_cosh,  std::cosh ((double) x))
UNARY (fe_sinh,  std::sinh ((double) x))
UNARY (fe_tanh,  std::tanh ((double) x))

BINARY (fe_atan2, std::atan2 ((double) x, (double) y))
BINARY (fe_fmod,  std::fmod ((double) x, (double) y))
BINARY (fe_pow,   std::pow ((double) x, (double) y))

// NOT std::remainder: the JS table defines _remainderf as x - Math.round(x/y)*y,
// and Math.round rounds halves upward where remainder() rounds half to even.
// Transcribed from the thing being matched rather than substituted for it.
BINARY (fe_remainder, (double) x - std::round ((double) x / (double) y) * (double) y)

// Precision-independent: exact for f32 inputs under either scheme.
UNARY  (fe_isnan,    std::isnan (x) ? 1.0f : 0.0f)
UNARY  (fe_isinf,    std::isinf (x) ? 1.0f : 0.0f)
BINARY (fe_max,      (x > y ? x : y))
BINARY (fe_min,      (x < y ? x : y))
BINARY (fe_copysign, std::copysign (x, y))

namespace
{
struct Link { const char* name; int arity; wasmtime_func_unchecked_callback_t fn; };

const Link kMathTable[] = {
    { "_abs", 1, fe_abs },
    { "_acosf", 1, fe_acos },   { "_asinf", 1, fe_asin },
    { "_atanf", 1, fe_atan },   { "_ceilf", 1, fe_ceil },
    { "_cosf", 1, fe_cos },     { "_expf", 1, fe_exp },
    { "_floorf", 1, fe_floor }, { "_logf", 1, fe_log },
    { "_log10f", 1, fe_log10 }, { "_roundf", 1, fe_round },
    { "_sinf", 1, fe_sin },     { "_sqrtf", 1, fe_sqrt },
    { "_tanf", 1, fe_tan },     { "_acoshf", 1, fe_acosh },
    { "_asinhf", 1, fe_asinh }, { "_atanhf", 1, fe_atanh },
    { "_coshf", 1, fe_cosh },   { "_sinhf", 1, fe_sinh },
    { "_tanhf", 1, fe_tanh },
    { "_isnanf", 1, fe_isnan }, { "_isinff", 1, fe_isinf },
    { "_atan2f", 2, fe_atan2 }, { "_fmodf", 2, fe_fmod },
    { "_powf", 2, fe_pow },     { "_remainderf", 2, fe_remainder },
    { "_max_f", 2, fe_max },    { "_min_f", 2, fe_min },
    { "_copysignf", 2, fe_copysign },
};

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

float mtof (int note) noexcept
{
    return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
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
    wasmtime_store_t*   store = nullptr;
    wasmtime_context_t* ctx   = nullptr;
    wasmtime_instance_t instance {};
    wasmtime_func_t     fInit {}, fCompute {}, fSetParam {};

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

    // Held by the sustain pedal after its key came up. The note number is
    // cleared so the same key can retrigger, but the voice keeps sounding
    // until the pedal lifts.
    bool sustained = false;
};

struct FaustEngineImpl
{
    // ONE module, compiled once by Cranelift, shared across every voice --
    // unlike the wasm3 path, where each voice had to parse its own because
    // m3_LoadModule consumes the module and it owns the instance's memory.
    // Compilation happens in wasmtime_module_new and is the expensive step, so
    // sharing it is what keeps per-voice load cost at instantiation only.
    wasm_engine_t*      engine = nullptr;
    wasmtime_module_t*  module = nullptr;
    wasmtime_linker_t*  linker = nullptr;
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

    // Diagnostics. A stuck note has two possible causes -- the engine mishandled
    // a note-off, or no note-off arrived -- and they need completely different
    // fixes. Counting both ends answers which without a debugger.
    std::atomic<int> notesOn { 0 }, notesOff { 0 }, sustainEvents { 0 };

    int graceFrames = 0, fadeFrames = 0;
    std::atomic<int> forcedStops { 0 };
};

FaustEngine::FaustEngine() : impl_ (std::make_unique<FaustEngineImpl>()) {}

FaustEngine::~FaustEngine()
{
    if (impl_ == nullptr) return;

    for (auto& v : impl_->voices)
        if (v.store != nullptr) wasmtime_store_delete (v.store);

    if (impl_->linker != nullptr) wasmtime_linker_delete (impl_->linker);
    if (impl_->module != nullptr) wasmtime_module_delete (impl_->module);
    if (impl_->engine != nullptr) wasm_engine_delete (impl_->engine);
}

// Creates N instances from the ALREADY-COMPILED module. Separated from load()
// so the voice count can change without paying Cranelift again -- which is what
// makes measuring a genome at one voice and then sizing the polyphony to the
// result affordable.
bool FaustEngine::instantiateVoices (int numVoices, juce::String& errOut)
{
    for (auto& v : impl_->voices)
        if (v.store != nullptr) wasmtime_store_delete (v.store);

    impl_->voices.clear();
    impl_->voices.resize ((size_t) numVoices);

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

        wasmtime_val_raw_t args[2];
        args[0].i32 = (std::int32_t) v.dspPtr;
        args[1].i32 = (std::int32_t) impl_->sampleRate;
        if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fInit, args, 2, &trap))
        { wasmtime_error_delete (err); errOut = "init failed"; return false; }
        if (trap != nullptr) { wasm_trap_delete (trap); errOut = "init trapped"; return false; }
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
    if (numVoices < 1 || maxBlockSize < 1) { r.detail = "voices and block size must be positive"; return r; }

    // ── metadata ────────────────────────────────────────────────────────────
    const auto parsed = juce::JSON::parse (jsonFile.loadFileAsString());

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
    juce::MemoryBlock mb;
    if (! wasmFile.loadFileAsData (mb)) { r.detail = "could not read the .wasm"; return r; }
    impl_->wasm.assign ((const std::uint8_t*) mb.getData(),
                        (const std::uint8_t*) mb.getData() + mb.getSize());

    impl_->sampleRate = sampleRate;
    impl_->maxBlockSize = maxBlockSize;
    impl_->mix.assign ((size_t) maxBlockSize, 0.0f);

    // A voice gets this long after note-off to show it is decaying before the
    // plugin stops it for them. 1.5 s is chosen so an exponential tail heading
    // for -60 dB at 8 s -- roughly the longest the sampler's SFZ artefacts
    // declare -- has already lost ~11 dB and is safely under the threshold.
    impl_->graceFrames = (int) (1.5 * sampleRate);
    impl_->fadeFrames  = (int) (0.2 * sampleRate);

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
    wasmtime_val_raw_t args[3];
    args[0].i32 = (std::int32_t) v.dspPtr;
    args[1].i32 = (std::int32_t) byteOffset;
    args[2].f32 = value;

    wasm_trap_t* trap = nullptr;
    if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fSetParam, args, 3, &trap))
        wasmtime_error_delete (err);
    if (trap != nullptr) wasm_trap_delete (trap);
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

    return juce::String ("wasmtime-") + WASMTIME_VERSION + "-" + os + "-" + arch;
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

    float peak = 0.0f;
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

        panic();
        // Parameter-driven, so forced release never engages: this must measure
        // the genome, not the plugin's handling of it.
        if (idxFreq >= 0) setParameter (idxFreq, mtof (p));
        if (idxGain >= 0) setParameter (idxGain, 1.0f);   // full velocity, the worst case
        if (idxGate >= 0) setParameter (idxGate, 1.0f);

        for (int b = 0; b < holdBlocks; ++b)
        {
            process (scratch, noMidi);
            peak = juce::jmax (peak, scratch.getMagnitude (0, impl_->maxBlockSize));
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
            peak = juce::jmax (peak, scratch.getMagnitude (0, impl_->maxBlockSize));
            if (capturing)
                capture.insert (capture.end(), scratch.getReadPointer (0),
                                scratch.getReadPointer (0) + impl_->maxBlockSize);
        }
    }

    const double elapsed = juce::Time::highResolutionTicksToSeconds (
                               juce::Time::getHighResolutionTicks() - t0);

    panic();
    impl_->outputGain = saved;
    peak_.store (0.0f, std::memory_order_relaxed);

    m.peak = peak;

    // Free-running, and labelled so. d3321de measured that holding a real block
    // cadence costs 2.3-2.9x more than rendering flat out, so this ranks
    // genomes against each other; it is not a headroom claim about a busy DAW.
    // Whether >=4x is the right bar is what rt-sweep.mjs still has to answer.
    const double audioSeconds = 3 * (2.0 + 3.0);
    m.rtf = elapsed > 0.0 ? audioSeconds / elapsed : 0.0;

    // Gate 0 §5 validated single-voice RTF as an exact predictor of polyphony,
    // 30/30, with a 1.05 per-voice penalty.
    m.capAt4x = 1;
    for (int v = 1; v <= 32; v *= 2)
        if (m.rtf / (v * 1.05) >= 4.0) m.capAt4x = v;

    // Computed AFTER the timing is taken, so the FFT never lands inside the
    // measured interval and inflates the genome's apparent cost.
    m.spectrogram = buildSpectrogram (capture, impl_->sampleRate);

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

        // init() rather than gate=0. A genome that ignores its gate ignores it
        // during a panic too, and 16 of 33 measured elites do exactly that.
        // Re-initialising zeroes the DSP struct, which no genome can decline.
        wasmtime_val_raw_t args[2];
        args[0].i32 = (std::int32_t) v.dspPtr;
        args[1].i32 = (std::int32_t) impl_->sampleRate;

        wasm_trap_t* trap = nullptr;
        if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fInit, args, 2, &trap))
            wasmtime_error_delete (err);
        if (trap != nullptr) wasm_trap_delete (trap);
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

void FaustEngine::noteOn (int note, int velocity) noexcept
{
    FaustVoice* chosen = nullptr;

    for (auto& v : impl_->voices)
        if (v.note < 0) { chosen = &v; break; }

    // Steal the oldest. Crude, and deliberately so until there is a reason to
    // be cleverer -- a released voice still ringing is indistinguishable here
    // from a held one, because nothing reads the module's envelope state.
    if (chosen == nullptr)
    {
        std::uint64_t oldest = ~0ull;
        for (auto& v : impl_->voices)
            if (v.startedAt < oldest) { oldest = v.startedAt; chosen = &v; }
    }

    if (chosen == nullptr) return;

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

            wasmtime_val_raw_t args[4];
            args[0].i32 = (std::int32_t) v.dspPtr;
            args[1].i32 = chunk;
            args[2].i32 = (std::int32_t) v.inPtrArr;
            args[3].i32 = (std::int32_t) v.outPtrArr;

            wasm_trap_t* trap = nullptr;
            auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fCompute, args, 4, &trap);

            if (err != nullptr) { wasmtime_error_delete (err); continue; }
            if (trap != nullptr) { wasm_trap_delete (trap); continue; }

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

            if (v.forcing)
            {
                const float step = 1.0f / (float) juce::jmax (1, impl_->fadeFrames);

                for (int i = 0; i < chunk; ++i)
                {
                    impl_->mix[(size_t) i] += src[i] * v.forceGain;
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

                    wasmtime_val_raw_t ia[2];
                    ia[0].i32 = (std::int32_t) v.dspPtr;
                    ia[1].i32 = (std::int32_t) impl_->sampleRate;
                    wasm_trap_t* itrap = nullptr;
                    if (auto* ierr = wasmtime_func_call_unchecked (v.ctx, &v.fInit, ia, 2, &itrap))
                        wasmtime_error_delete (ierr);
                    if (itrap != nullptr) wasm_trap_delete (itrap);
                }
            }
            else
            {
                for (int i = 0; i < chunk; ++i)
                    impl_->mix[(size_t) i] += src[i];
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
                }
            }
            else
            {
                v.quietBlocks = 0;
            }
        }

        // outputs == 1 for this substrate, so the mono result is copied across
        // the pair. See the header: this is the opposite of SamplerEngine.
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, pos, impl_->mix.data(), chunk, impl_->outputGain);

        pos = chunkEnd;
    }

    // §4.2's guard. The failure this defends against is inaudible and passes
    // every validator, so the evidence has to be collected rather than assumed.
    const float blockPeak = buffer.getMagnitude (0, numFrames);
    float seen = peak_.load (std::memory_order_relaxed);
    while (blockPeak > seen && ! peak_.compare_exchange_weak (seen, blockPeak)) {}

    blocks_.fetch_add (1, std::memory_order_relaxed);
}
