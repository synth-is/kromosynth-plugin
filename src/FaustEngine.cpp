// FaustEngine.cpp — see the header for why each decision here is what it is.
//
// The wasm3 driving code is a port of probe/gate0/host_wasm3.c, which produced
// bit-identical output to the platform reference across the 120-elite archive.
// Divergence from that file should be treated as a bug in this one.

#include "FaustEngine.h"

#include <wasm.h>
#include <wasmtime.h>

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

    int  note = -1;          // -1 means free
    bool gateOpen = false;
    std::uint64_t startedAt = 0;   // for voice stealing: oldest first
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

    impl_->voices.resize ((size_t) numVoices);

    for (auto& v : impl_->voices)
    {
        v.store = wasmtime_store_new (impl_->engine, nullptr, nullptr);
        v.ctx = wasmtime_store_context (v.store);

        wasm_trap_t* trap = nullptr;
        if (auto* err = wasmtime_linker_instantiate (impl_->linker, v.ctx, impl_->module,
                                                     &v.instance, &trap))
        { wasmtime_error_delete (err); r.detail = "instantiate failed"; return r; }
        if (trap != nullptr) { wasm_trap_delete (trap); r.detail = "instantiate trapped"; return r; }

        const auto getFunc = [&] (const char* name, wasmtime_func_t& out) -> bool
        {
            wasmtime_extern_t item;
            if (! wasmtime_instance_export_get (v.ctx, &v.instance, name, std::strlen (name), &item)
                || item.kind != WASMTIME_EXTERN_FUNC)
                return false;
            out = item.of.func;
            return true;
        };

        if (! getFunc ("init", v.fInit))               { r.detail = "missing export: init"; return r; }
        if (! getFunc ("compute", v.fCompute))         { r.detail = "missing export: compute"; return r; }
        if (! getFunc ("setParamValue", v.fSetParam))  { r.detail = "missing export: setParamValue"; return r; }

        wasmtime_extern_t memItem;
        if (! wasmtime_instance_export_get (v.ctx, &v.instance, "memory", 6, &memItem)
            || memItem.kind != WASMTIME_EXTERN_MEMORY)
        { r.detail = "module exports no memory"; return r; }

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
        v.outBufs   = v.inBufs + (std::uint32_t) impl_->inputs * (std::uint32_t) maxBlockSize * ss;

        const std::uint32_t end =
            v.outBufs + (std::uint32_t) impl_->outputs * (std::uint32_t) maxBlockSize * ss;

        if ((std::size_t) end > v.memBytes)
        {
            r.detail = "layout " + juce::String ((int) end) + " B exceeds module memory "
                     + juce::String ((int) v.memBytes) + " B at block size "
                     + juce::String (maxBlockSize);
            return r;
        }

        auto* h32 = reinterpret_cast<std::uint32_t*> (v.memory);
        for (int c = 0; c < impl_->inputs; ++c)
            h32[(v.inPtrArr >> 2) + c] = v.inBufs + (std::uint32_t) maxBlockSize * ss * (std::uint32_t) c;
        for (int c = 0; c < impl_->outputs; ++c)
            h32[(v.outPtrArr >> 2) + c] = v.outBufs + (std::uint32_t) maxBlockSize * ss * (std::uint32_t) c;

        wasmtime_val_raw_t args[2];
        args[0].i32 = (std::int32_t) v.dspPtr;
        args[1].i32 = (std::int32_t) sampleRate;
        if (auto* err = wasmtime_func_call_unchecked (v.ctx, &v.fInit, args, 2, &trap))
        { wasmtime_error_delete (err); r.detail = "init failed"; return r; }
        if (trap != nullptr) { wasm_trap_delete (trap); r.detail = "init trapped"; return r; }
    }

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

void FaustEngine::panic() noexcept
{
    if (! loaded_) return;

    for (auto& v : impl_->voices)
    {
        v.note = -1;
        v.gateOpen = false;

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
        setParam (v, byteOffset, value);
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
    for (auto& v : impl_->voices)
    {
        if (v.note != note || ! v.gateOpen) continue;

        v.gateOpen = false;
        if (impl_->idxGate >= 0) setParam (v, impl_->idxGate, 0.0f);

        // The voice is NOT freed here. The module's release tail is still
        // running and nothing in the wasm ABI reports when it has finished, so
        // freeing on note-off would cut every release short. It stays claimed
        // until stolen. That is a known limitation of this skeleton and the
        // first thing polyphony work should fix.
        v.note = -1;
    }
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
            else if (message.isAllNotesOff() || message.isAllSoundOff()) panic();

            ++event;
        }

        const int chunk = chunkEnd - pos;
        if (chunk <= 0) break;

        std::fill (impl_->mix.begin(), impl_->mix.begin() + chunk, 0.0f);

        for (auto& v : impl_->voices)
        {
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

            for (int i = 0; i < chunk; ++i)
                impl_->mix[(size_t) i] += src[i];
        }

        // outputs == 1 for this substrate, so the mono result is copied across
        // the pair. See the header: this is the opposite of SamplerEngine.
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, pos, impl_->mix.data(), chunk);

        pos = chunkEnd;
    }

    // §4.2's guard. The failure this defends against is inaudible and passes
    // every validator, so the evidence has to be collected rather than assumed.
    const float blockPeak = buffer.getMagnitude (0, numFrames);
    float seen = peak_.load (std::memory_order_relaxed);
    while (blockPeak > seen && ! peak_.compare_exchange_weak (seen, blockPeak)) {}

    blocks_.fetch_add (1, std::memory_order_relaxed);
}
