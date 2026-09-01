// faust-probe — drives FaustEngine directly, with no plugin wrapper and no
// audio device.
//
// DEVELOPER TOOL, never shipped. Same conventions as engine-probe and sfz-probe.
//
// ── Why this exists before any DAW does ─────────────────────────────────────
//
// engine-probe was written because auval's "1 Channel Test" passes on a plugin
// that renders silence -- it checks only that rendering does not crash. The
// same hole applies here and is worse, because a Faust module that loads,
// instantiates, links every import and computes happily can still emit nothing
// if the gate never opens or the parameter offsets are wrong. All of those
// failures look identical from outside: peak 0.000000, no error anywhere.
//
// So the first question asked of this engine is not "does it run" but "does it
// make a sound", and it is asked here rather than in GarageBand where "no notes
// played" and "broken parameter map" are indistinguishable.
//
// ── Usage ──────────────────────────────────────────────────────────────────
//
//     faust-probe <module.wasm> <module.json> [note] [velocity] [voices]
//
// Plays a note, holds it, releases it, and reports the peak of each phase. A
// silent attack and a silent release mean different things.

#include "../../src/FaustEngine.h"

#include <wasmtime.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// The null test.
//
// Cross-runtime agreement -- the interpreter and the JIT matching to six
// decimals -- proves the two engines agree with EACH OTHER. It says nothing
// about whether either agrees with the platform, because both are running the
// same port of the same layout. A shared misreading of a parameter offset
// would be invisible to it.
//
// So this replays a Gate 0 .plan through FaustEngine and compares against that
// experiment's reference render, which came from the platform's own path
// (faustwasm glue over V8; see EXPERIMENT_01_3_DESKTOP_RESULT §6's second
// stated deviation). Gate 0 found wasm3, WAMR and wasmtime all BIT-IDENTICAL
// to it, so anything short of zero difference here is this engine's fault and
// not a tolerance to be argued about.
//
// The -50.5 dB libm divergence §1 records is exactly the class of bug this
// catches and nothing else does: inaudible in isolation, obvious the moment a
// user A/Bs an instrument against the preview it was chosen from.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
struct PlanEvent { int atSample = 0, byteOffset = 0; float value = 0.0f; bool isFreq = false; };

struct Plan
{
    int sampleRate = 0, blockSize = 0, structBytes = 0, inputs = 0, outputs = 0;
    double seconds = 0.0;
    std::vector<PlanEvent> events;
};

bool readPlan (const juce::File& file, Plan& plan)
{
    juce::StringArray tokens;
    tokens.addTokens (file.loadFileAsString(), " \t\r\n", "");
    tokens.removeEmptyStrings();

    for (int i = 0; i < tokens.size();)
    {
        const auto key = tokens[i++];
        const auto next = [&]() -> juce::String { return i < tokens.size() ? tokens[i++] : juce::String{}; };

        if      (key == "sampleRate")  plan.sampleRate  = next().getIntValue();
        else if (key == "blockSize")   plan.blockSize   = next().getIntValue();
        else if (key == "structBytes") plan.structBytes = next().getIntValue();
        else if (key == "inputs")      plan.inputs      = next().getIntValue();
        else if (key == "outputs")     plan.outputs     = next().getIntValue();
        else if (key == "seconds")     plan.seconds     = next().getDoubleValue();
        else if (key == "event")
        {
            PlanEvent e;
            e.atSample   = next().getIntValue();
            e.byteOffset = next().getIntValue();
            e.value      = (float) next().getDoubleValue();
            e.isFreq     = (next() == "freq");
            plan.events.push_back (e);
        }
        else { std::printf ("unknown plan key '%s'\n", key.toRawUTF8()); return false; }
    }

    return plan.sampleRate > 0 && plan.blockSize > 0 && plan.outputs > 0;
}

int runNullTest (const juce::File& wasmFile, const juce::File& jsonFile,
                 const juce::File& planFile, const juce::File& refFile)
{
    Plan plan;
    if (! readPlan (planFile, plan)) { std::puts ("could not read the plan"); return 2; }

    std::printf ("plan       %d Hz, %d-frame blocks, %.1f s, %d event(s)\n",
                 plan.sampleRate, plan.blockSize, plan.seconds, (int) plan.events.size());

    FaustEngine engine;

    // ONE voice. host_wasm3.c detunes voice i by 2^(i/12), so voice 0 alone is
    // the only configuration comparable to a reference rendered at the plan's
    // stated frequency.
    const auto load = engine.load (wasmFile, jsonFile, (double) plan.sampleRate,
                                   plan.blockSize, 1);
    if (! load.ok) { std::printf ("load failed: %s\n", load.detail.toRawUTF8()); return 1; }

    const int total = (int) (plan.seconds * plan.sampleRate + 0.5);
    std::vector<float> rendered ((size_t) total, 0.0f);

    juce::AudioBuffer<float> buffer { 1, plan.blockSize };
    const juce::MidiBuffer noMidi;

    size_t nextEvent = 0;
    int written = 0;

    while (written < total)
    {
        // Events land on block boundaries, exactly as the probe host applied
        // them. Applying them mid-block would be more correct and would NOT
        // match the reference, which is the point: this reproduces the render,
        // it does not improve on it.
        while (nextEvent < plan.events.size()
               && plan.events[nextEvent].atSample <= written)
        {
            engine.setParameter (plan.events[nextEvent].byteOffset,
                                 plan.events[nextEvent].value);
            ++nextEvent;
        }

        const int take = juce::jmin (plan.blockSize, total - written);
        buffer.setSize (1, plan.blockSize, false, false, true);
        engine.process (buffer, noMidi);
        std::copy_n (buffer.getReadPointer (0), take, rendered.begin() + written);
        written += take;
    }

    // ── compare ─────────────────────────────────────────────────────────────
    juce::MemoryBlock refBytes;
    if (! refFile.loadFileAsData (refBytes)) { std::puts ("could not read the reference"); return 2; }

    const auto* ref = static_cast<const float*> (refBytes.getData());
    const int refCount = (int) (refBytes.getSize() / sizeof (float));
    const int n = juce::jmin (refCount, total);

    double maxAbs = 0.0, sumSq = 0.0, refSq = 0.0;
    double peakOurs = 0.0, peakRef = 0.0;
    int firstDiff = -1;

    for (int i = 0; i < n; ++i)
    {
        const double d = (double) rendered[(size_t) i] - (double) ref[i];
        if (std::abs (d) > maxAbs) maxAbs = std::abs (d);
        if (d != 0.0 && firstDiff < 0) firstDiff = i;
        sumSq += d * d;
        refSq += (double) ref[i] * ref[i];
        peakOurs = juce::jmax (peakOurs, (double) std::abs (rendered[(size_t) i]));
        peakRef  = juce::jmax (peakRef,  (double) std::abs (ref[i]));
    }

    const double diffRms = std::sqrt (sumSq / juce::jmax (1, n));
    const double refRms  = std::sqrt (refSq / juce::jmax (1, n));
    const double dbBelow = diffRms == 0.0 ? -1000.0 : 20.0 * std::log10 (diffRms / refRms);

    std::printf ("samples    ours %d, reference %d, compared %d\n", total, refCount, n);
    std::printf ("peak       ours %.6f   reference %.6f\n", peakOurs, peakRef);

    // §4.2 again: a silent render nulls perfectly against nothing.
    if (peakRef == 0.0) { std::puts ("\nreference is silent -- the test proves nothing"); return 1; }
    if (peakOurs == 0.0) { std::puts ("\nFAIL: our render is silent"); return 1; }

    std::printf ("maxAbsDiff %.6e\n", maxAbs);
    if (firstDiff >= 0) std::printf ("firstDiff  sample %d (%.4f s)\n", firstDiff,
                                     (double) firstDiff / plan.sampleRate);
    std::printf ("diff       %.2f dB below reference RMS\n", dbBelow);

    if (maxAbs == 0.0)
    {
        std::puts ("\nPASS: bit-identical to the platform reference.");
        return 0;
    }

    // Gate 0 got zero from three runtimes. Anything else is this engine.
    std::printf ("\nFAIL: not bit-identical. Gate 0 measured wasm3, WAMR and wasmtime\n"
                 "all at exactly zero against this reference, so a non-zero difference\n"
                 "here is a defect in FaustEngine, not a tolerance.\n");
    return 1;
}

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

float renderPhase (FaustEngine& engine, juce::AudioBuffer<float>& buffer,
                   const juce::MidiBuffer& midi, int blocks)
{
    float peak = 0.0f;

    for (int b = 0; b < blocks; ++b)
    {
        // MIDI only on the first block of the phase; the rest are empty.
        const juce::MidiBuffer thisBlock = (b == 0) ? midi : juce::MidiBuffer{};
        engine.process (buffer, thisBlock);
        peak = juce::jmax (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
    }

    return peak;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::puts ("usage: faust-probe <module.wasm> <module.json> [note] [velocity] [voices]");
        std::puts ("       faust-probe <module.wasm> <module.json> --plan <x.plan> --ref <x.ref.f32>");
        return 2;
    }

    const juce::File wasmFile { juce::String (argv[1]) };
    const juce::File jsonFile { juce::String (argv[2]) };
    const int note     = argc > 3 ? std::atoi (argv[3]) : 60;
    const int velocity = argc > 4 ? std::atoi (argv[4]) : 100;
    const int voices   = argc > 5 ? std::atoi (argv[5]) : 8;

    int planArg = -1, refArg = -1;
    for (int i = 3; i + 1 < argc; ++i)
    {
        if (juce::String (argv[i]) == "--plan") planArg = i + 1;
        if (juce::String (argv[i]) == "--ref")  refArg  = i + 1;
    }

    if (planArg > 0 && refArg > 0)
        return runNullTest (wasmFile, jsonFile,
                            juce::File (juce::String (argv[planArg])),
                            juce::File (juce::String (argv[refArg])));

    std::printf ("runtime    wasmtime/Cranelift, C API\n");

    FaustEngine engine;

    const auto load = engine.load (wasmFile, jsonFile, kSampleRate, kBlockSize, voices);

    std::printf ("load       ok=%d  %s\n", (int) load.ok, load.detail.toRawUTF8());

    if (! load.ok)
        return 1;

    std::printf ("dsp        \"%s\"  struct=%d B  outputs=%d  voices=%d\n",
                 load.dspName.toRawUTF8(), load.structBytes, load.outputs, load.voices);
    // Every control this genome publishes, so "can the decay be controlled"
    // is answered by looking rather than by hoping there is a `dec`.
    std::puts ("\ncontrols");
    for (const auto& prm : engine.parameters())
        std::printf ("  %-12s %-8s init %-9g range %g .. %g\n",
                     prm.label.toRawUTF8(), prm.isButton ? "button" : "slider",
                     prm.init, prm.minimum, prm.maximum);
    std::puts ("");

    // --set label=value, applied after load and before the note. Overrides the
    // genome's own init, which is what a user turning a knob would do.
    for (int i = 3; i + 1 < argc; ++i)
    {
        if (juce::String (argv[i]) != "--set") continue;

        const juce::String spec (argv[i + 1]);
        const auto label = spec.upToFirstOccurrenceOf ("=", false, false);
        const auto value = spec.fromFirstOccurrenceOf ("=", false, false).getFloatValue();

        bool found = false;
        for (const auto& prm : engine.parameters())
            if (prm.label == label)
            {
                engine.setParameter (prm.byteOffset, value);
                std::printf ("set        %s = %g\n", label.toRawUTF8(), value);
                found = true;
            }

        if (! found)
            std::printf ("set        %s NOT PUBLISHED by this genome — ignored\n",
                         label.toRawUTF8());
    }

    std::printf ("params     freq=%s  gain=%s  gate=%s\n",
                 load.hasFreq ? "yes" : "NO", load.hasGain ? "yes" : "NO",
                 load.hasGate ? "yes" : "NO");

    // A module with no gate cannot be articulated and will either drone or stay
    // silent. Worth saying out loud rather than letting it read as a zero peak.
    if (! load.hasGate)
        std::puts ("           NOTE: no `gate` parameter -- this module cannot be triggered");

    juce::AudioBuffer<float> buffer { 2, kBlockSize };

    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0);

    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::noteOff (1, note), 0);

    std::printf ("\nnote %d  velocity %d  %d Hz  blocks of %d\n\n",
                 note, velocity, (int) kSampleRate, kBlockSize);

    // The third phase has to outlast the forced-release grace period (1.5 s)
    // plus its fade (0.2 s), or a drone would still be sounding at the end for
    // the entirely uninteresting reason that nothing had decided yet.
    const float held     = renderPhase (engine, buffer, on,  40);   // ~0.43 s
    const float released = renderPhase (engine, buffer, off, 40);   // ~0.43 s
    const float settled  = renderPhase (engine, buffer, {},  250);  // ~2.7 s

    // settled is the MAX over its phase, so it still contains audio from
    // before any forced fade began. The level right at the end is the only
    // thing that answers "is it quiet now".
    const float ending   = renderPhase (engine, buffer, {},  8);    // ~85 ms

    std::puts ("phase              peak   verdict");
    std::printf ("held           %8.6f   %s\n", held,     held     > 1.0e-5f ? "audible" : "SILENT");
    std::printf ("released       %8.6f   %s\n", released, released > 1.0e-5f ? "ringing" : "silent");
    std::printf ("settled        %8.6f   %s\n", settled,  settled  > 1.0e-5f ? "still ringing" : "silent");
    std::printf ("ending         %8.6f   %s\n", ending,
                 ending > 1.0e-5f ? "STILL SOUNDING" : "silent");
    std::printf ("forced stops   %8d   %s\n", engine.forcedStops(),
                 engine.forcedStops() > 0 ? "genome would not stop; plugin stopped it"
                                          : "genome decayed on its own");

    std::printf ("\nblocks rendered: %llu   peak since load: %f\n",
                 (unsigned long long) engine.blocksRendered(), engine.peakSinceReset());

    if (held <= 1.0e-5f)
    {
        std::puts ("\nFAIL: the held note produced no audio.");
        return 1;
    }

    std::puts ("\nThe engine produced audio.");
    return 0;
}
