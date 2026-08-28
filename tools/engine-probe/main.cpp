// engine-probe — exercises SamplerEngine's channel-layout paths directly.
//
// DEVELOPER TOOL, never shipped.
//
// ── Why this exists ──────────────────────────────────────────────────────────
//
// SamplerEngine::process has two paths:
//
//   numChannels >= 2  ->  render in place into the host's buffer
//   numChannels == 1  ->  render into a stereo scratch, sum to mono
//
// Everything that has ever tested this plugin used the first one. auval,
// pluginval and the Reaper harness are all stereo, and neither validator has a
// channel-configuration option. So the mono path shipped untested.
//
// It then got exercised immediately: STAGE0_FINDINGS.md L8 records GarageBand
// being given the mono variant and reporting peak=0.000000 over 758 blocks.
// That reading is ambiguous -- no notes played looks identical to a broken
// mono path -- and the way to disambiguate is to drive the engine directly
// with MIDI we know exists.
//
// auval does run a "1 Channel Test" and it passes, but it only checks that
// rendering does not crash. It never checks for non-silence. Same shape as
// section I5: a green result that was never capable of being red.
//
// ── Usage ────────────────────────────────────────────────────────────────────
//
//     engine-probe <instrument.sfz> [note] [velocity]
//
// Renders the same note through 1-, 2- and 6-channel buffers and reports the
// peak of each. Mono is expected to sit near half the stereo peak, since the
// mono path averages the two rendered channels.

#include "../../src/SamplerEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr int kBlocks = 200; // ~2.1 s

struct Result
{
    float peak = 0.0f;
    double rms = 0.0;
    int blocks = 0;
};

Result renderWith (SamplerEngine& engine, int numChannels, int note, int velocity)
{
    engine.reset();

    juce::AudioBuffer<float> buffer (numChannels, kBlockSize);

    Result result;
    double sumSquares = 0.0;
    juce::int64 samples = 0;

    for (int block = 0; block < kBlocks; ++block)
    {
        juce::MidiBuffer midi;

        // Note on in the first block, off two thirds of the way through, so
        // the release tail is inside the render.
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), 0);
        else if (block == (kBlocks * 2) / 3)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);

        buffer.clear();
        engine.process (buffer, midi);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = buffer.getReadPointer (ch);

            for (int i = 0; i < kBlockSize; ++i)
            {
                result.peak = juce::jmax (result.peak, std::abs (data[i]));
                sumSquares += double (data[i]) * double (data[i]);
                ++samples;
            }
        }

        ++result.blocks;
    }

    result.rms = samples > 0 ? std::sqrt (sumSquares / double (samples)) : 0.0;
    return result;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::puts ("usage: engine-probe <instrument.sfz> [note] [velocity]");
        return 2;
    }

    const juce::File sfzFile { juce::String (argv[1]) };
    const int note = argc > 2 ? std::atoi (argv[2]) : 60;
    const int velocity = argc > 3 ? std::atoi (argv[3]) : 100;

    SamplerEngine engine;
    engine.prepare (kSampleRate, kBlockSize);

    const auto load = engine.load (sfzFile);

    std::printf ("load       ok=%d regions=%d voices=%d %.1f ms  tail %.3f s\n",
                 load.ok ? 1 : 0, load.numRegions, load.numVoices,
                 load.loadMs, load.tailSeconds);

    if (! load.ok)
    {
        std::printf ("FAILED: %s\n", load.detail.toRawUTF8());
        return 1;
    }

    std::printf ("note %d velocity %d, %d blocks of %d at %.0f Hz\n\n",
                 note, velocity, kBlocks, kBlockSize, kSampleRate);

    std::printf ("%-10s %12s %12s   %s\n", "channels", "peak", "rms", "verdict");

    float stereoPeak = 0.0f;
    int failures = 0;

    for (const int channels : { 1, 2, 6 })
    {
        const auto r = renderWith (engine, channels, note, velocity);

        if (channels == 2)
            stereoPeak = r.peak;

        const char* verdict = r.peak > 0.0f ? "audible" : "SILENT";

        if (r.peak <= 0.0f)
            ++failures;

        std::printf ("%-10d %12.6f %12.6f   %s\n", channels, r.peak, r.rms, verdict);
    }

    // The mono path averages the two rendered channels, so for a broadly
    // centred source it should land near the stereo peak rather than at half
    // of it -- but a hard-panned or decorrelated source legitimately drops.
    // Reported rather than asserted: this is a measuring instrument, and a
    // threshold here would be a guess dressed as a check.
    if (stereoPeak > 0.0f)
        std::printf ("\nmono/stereo peak ratio: %.3f\n",
                     double (renderWith (engine, 1, note, velocity).peak / stereoPeak));

    if (failures > 0)
    {
        std::printf ("\n%d channel configuration(s) rendered SILENCE.\n", failures);
        return 1;
    }

    std::puts ("\nAll channel configurations produced audio.");
    return 0;
}
