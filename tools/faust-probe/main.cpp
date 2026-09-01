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

#include <cstdio>
#include <cstdlib>

namespace
{
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
        return 2;
    }

    const juce::File wasmFile { juce::String (argv[1]) };
    const juce::File jsonFile { juce::String (argv[2]) };
    const int note     = argc > 3 ? std::atoi (argv[3]) : 60;
    const int velocity = argc > 4 ? std::atoi (argv[4]) : 100;
    const int voices   = argc > 5 ? std::atoi (argv[5]) : 8;

    std::printf ("runtime    wasmtime/Cranelift, C API\n");

    FaustEngine engine;

    const auto load = engine.load (wasmFile, jsonFile, kSampleRate, kBlockSize, voices);

    std::printf ("load       ok=%d  %s\n", (int) load.ok, load.detail.toRawUTF8());

    if (! load.ok)
        return 1;

    std::printf ("dsp        \"%s\"  struct=%d B  outputs=%d  voices=%d\n",
                 load.dspName.toRawUTF8(), load.structBytes, load.outputs, load.voices);
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

    const float held     = renderPhase (engine, buffer, on,  40);   // ~0.43 s
    const float released = renderPhase (engine, buffer, off, 40);
    const float settled  = renderPhase (engine, buffer, {},  40);

    std::puts ("phase              peak   verdict");
    std::printf ("held           %8.6f   %s\n", held,     held     > 1.0e-5f ? "audible" : "SILENT");
    std::printf ("released       %8.6f   %s\n", released, released > 1.0e-5f ? "ringing" : "silent");
    std::printf ("settled        %8.6f   %s\n", settled,  settled  > 1.0e-5f ? "still ringing" : "silent");

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
