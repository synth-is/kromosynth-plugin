// FaustEngine.cpp — see the header for why each decision here is what it is.
//
// The wasm3 driving code is a port of probe/gate0/host_wasm3.c, which produced
// bit-identical output to the platform reference across the 120-elite archive.
// Divergence from that file should be treated as a bug in this one.

#include "FaustEngine.h"

#include <wasm3.h>

#include <algorithm>
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

#define UNARY(NAME, EXPR)                                    \
    static m3ApiRawFunction(NAME) {                          \
        m3ApiReturnType(float);                              \
        m3ApiGetArg(float, x);                               \
        m3ApiReturn((float)(EXPR));                          \
    }
#define BINARY(NAME, EXPR)                                   \
    static m3ApiRawFunction(NAME) {                          \
        m3ApiReturnType(float);                              \
        m3ApiGetArg(float, x);                               \
        m3ApiGetArg(float, y);                               \
        m3ApiReturn((float)(EXPR));                          \
    }

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
struct Link { const char* name; const char* sig; M3RawCall fn; };

const Link kMathTable[] = {
    { "_abs",        "f(f)",  fe_abs },
    { "_acosf",      "f(f)",  fe_acos },   { "_asinf",  "f(f)", fe_asin },
    { "_atanf",      "f(f)",  fe_atan },   { "_ceilf",  "f(f)", fe_ceil },
    { "_cosf",       "f(f)",  fe_cos },    { "_expf",   "f(f)", fe_exp },
    { "_floorf",     "f(f)",  fe_floor },  { "_logf",   "f(f)", fe_log },
    { "_log10f",     "f(f)",  fe_log10 },  { "_roundf", "f(f)", fe_round },
    { "_sinf",       "f(f)",  fe_sin },    { "_sqrtf",  "f(f)", fe_sqrt },
    { "_tanf",       "f(f)",  fe_tan },    { "_acoshf", "f(f)", fe_acosh },
    { "_asinhf",     "f(f)",  fe_asinh },  { "_atanhf", "f(f)", fe_atanh },
    { "_coshf",      "f(f)",  fe_cosh },   { "_sinhf",  "f(f)", fe_sinh },
    { "_tanhf",      "f(f)",  fe_tanh },
    { "_isnanf",     "f(f)",  fe_isnan },  { "_isinff", "f(f)", fe_isinf },
    { "_atan2f",     "f(ff)", fe_atan2 },  { "_fmodf",  "f(ff)", fe_fmod },
    { "_powf",       "f(ff)", fe_pow },    { "_remainderf", "f(ff)", fe_remainder },
    { "_max_f",      "f(ff)", fe_max },    { "_min_f",  "f(ff)", fe_min },
    { "_copysignf",  "f(ff)", fe_copysign },
};

constexpr int kVoiceStackBytes = 256 * 1024;   // as measured in host_wasm3.c

float mtof (int note) noexcept
{
    return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────

struct FaustVoice
{
    IM3Runtime  runtime = nullptr;
    IM3Module   module  = nullptr;
    IM3Function fInit = nullptr, fCompute = nullptr, fSetParam = nullptr;

    std::uint8_t* memory = nullptr;
    std::size_t memBytes = 0;

    std::uint32_t dspPtr = 0, inPtrArr = 0, outPtrArr = 0, inBufs = 0, outBufs = 0;

    int  note = -1;          // -1 means free
    bool gateOpen = false;
    std::uint64_t startedAt = 0;   // for voice stealing: oldest first
};

struct FaustEngineImpl
{
    IM3Environment env = nullptr;
    std::vector<std::uint8_t> wasm;
    std::vector<FaustVoice> voices;

    int structBytes = 0, inputs = 0, outputs = 1;
    int maxBlockSize = 0;
    double sampleRate = 48000.0;

    // Byte offsets into the DSP struct. -1 when the module does not publish
    // the parameter, which is legal: not every evolved genome carries a gate.
    int idxFreq = -1, idxGain = -1, idxGate = -1;

    juce::String dspName;
    std::uint64_t counter = 0;

    // Scratch for the mono module's output before it reaches the buffer.
    std::vector<float> mix;
};

FaustEngine::FaustEngine() : impl_ (std::make_unique<FaustEngineImpl>()) {}

FaustEngine::~FaustEngine()
{
    if (impl_ != nullptr)
    {
        for (auto& v : impl_->voices)
            if (v.runtime != nullptr)
                m3_FreeRuntime (v.runtime);

        if (impl_->env != nullptr)
            m3_FreeEnvironment (impl_->env);
    }
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

    if (impl_->env == nullptr) impl_->env = m3_NewEnvironment();
    if (impl_->env == nullptr) { r.detail = "m3_NewEnvironment failed"; return r; }

    for (auto& v : impl_->voices)
        if (v.runtime != nullptr) m3_FreeRuntime (v.runtime);

    impl_->voices.clear();
    impl_->voices.resize ((size_t) numVoices);

    for (auto& v : impl_->voices)
    {
        v.runtime = m3_NewRuntime (impl_->env, kVoiceStackBytes, nullptr);
        if (v.runtime == nullptr) { r.detail = "m3_NewRuntime failed"; return r; }

        // Each voice parses its own module: LoadModule consumes it and the
        // module owns the memory the instance runs in, so they cannot be shared.
        if (auto err = m3_ParseModule (impl_->env, &v.module, impl_->wasm.data(),
                                       (std::uint32_t) impl_->wasm.size()))
        { r.detail = juce::String ("m3_ParseModule: ") + err; return r; }

        if (auto err = m3_LoadModule (v.runtime, v.module))
        { r.detail = juce::String ("m3_LoadModule: ") + err; return r; }

        for (const auto& link : kMathTable)
        {
            const auto err = m3_LinkRawFunction (v.module, "env", link.name, link.sig, link.fn);

            // Not an error: the module simply does not import that name.
            if (err != nullptr && err != m3Err_functionLookupFailed)
            { r.detail = juce::String ("link ") + link.name + ": " + err; return r; }
        }

        if (auto err = m3_FindFunctionIn (&v.fInit, v.module, "init"))
        { r.detail = juce::String ("find init: ") + err; return r; }
        if (auto err = m3_FindFunctionIn (&v.fCompute, v.module, "compute"))
        { r.detail = juce::String ("find compute: ") + err; return r; }
        if (auto err = m3_FindFunctionIn (&v.fSetParam, v.module, "setParamValue"))
        { r.detail = juce::String ("find setParamValue: ") + err; return r; }

        std::size_t bytes = 0;
        v.memory = m3_GetMemory (v.module, &bytes, 0);
        v.memBytes = bytes;
        if (v.memory == nullptr) { r.detail = "module exports no memory"; return r; }

        // Layout, identical to layoutMono() in host_wasm3.c and to
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

        std::uint32_t a0 = v.dspPtr, a1 = (std::uint32_t) (int) sampleRate;
        const void* args[2] = { &a0, &a1 };
        if (auto err = m3_Call (v.fInit, 2, args))
        { r.detail = juce::String ("init: ") + err; return r; }
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
    std::uint32_t a0 = v.dspPtr, a1 = (std::uint32_t) byteOffset;
    const void* args[3] = { &a0, &a1, &value };
    m3_Call (v.fSetParam, 3, args);
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
            else if (message.isAllNotesOff() || message.isAllSoundOff()) reset();

            ++event;
        }

        const int chunk = chunkEnd - pos;
        if (chunk <= 0) break;

        std::fill (impl_->mix.begin(), impl_->mix.begin() + chunk, 0.0f);

        for (auto& v : impl_->voices)
        {
            std::uint32_t a0 = v.dspPtr, a1 = (std::uint32_t) chunk,
                          a2 = v.inPtrArr, a3 = v.outPtrArr;
            const void* args[4] = { &a0, &a1, &a2, &a3 };

            if (m3_Call (v.fCompute, 4, args) != nullptr) continue;

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
