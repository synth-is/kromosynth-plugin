// SPDX-License-Identifier: AGPL-3.0-or-later
// synthis-et4.18: the native reference for the sampler spike. Renders one note of an SFZ through the NATIVE sfizz
// (the static libraries the gate's build already made, build/verify-nocopy) the way SamplerEngine drives it
// (setSampleRate, setSamplesPerBlock, loadSfzFile, noteOn at delay 0, renderBlock in host-sized blocks) and writes
// the left channel as raw float32, so scripts/webclap-sampler-check.mjs can compare the WCLAP's render with it.
// Developer tool, NOT part of any build or the gate; scripts/webclap-sampler.sh native compiles and runs it.
//
//   webclap-sampler-native <file.sfz> <key> <frames> <out.f32> [block 512] [rate 48000]
//   webclap-sampler-native <file.sfz> time <voices>      native cost of N held notes (keys 69, 68, ...), 48 kHz,
//                                                        block 512, 0.5 s timed after a warm-up block, best of 3

#include <sfizz.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Freewheeling makes the file pool load synchronously (Synth.cpp:1115), which an offline tool needs, but it also
// switches sfizz to its freewheeling sample quality, a different (higher, slower) interpolator from the live one.
// SamplerEngine never freewheels, and neither does the WCLAP, so the reference renders at the LIVE quality.
static void freewheelAtLiveQuality (sfz::Sfizz& synth)
{
    synth.enableFreeWheeling();
    synth.setSampleQuality (sfz::Sfizz::ProcessFreewheeling, synth.getSampleQuality (sfz::Sfizz::ProcessLive));
    synth.setOscillatorQuality (sfz::Sfizz::ProcessFreewheeling, synth.getOscillatorQuality (sfz::Sfizz::ProcessLive));
}

static int timeVoices (const char* sfz, int voices)
{
    double best = 1e9;
    for (int run = 0; run < 3; ++run)
    {
        sfz::Sfizz synth;
        synth.setSampleRate (48000.0f);
        synth.setSamplesPerBlock (512);
        freewheelAtLiveQuality (synth);
        if (! synth.loadSfzFile (sfz)) return 1;
        std::vector<float> l (512), r (512);
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < voices; ++i) synth.noteOn (0, 69 - i, 102);
        synth.renderBlock (ch, 512);
        const auto t0 = std::chrono::steady_clock::now();
        for (int f = 0; f < 24000; f += 512) synth.renderBlock (ch, 512);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() * 2.0;
        if (ms < best) best = ms;
        if (synth.getNumActiveVoices() != voices) std::printf ("(only %d voices active)\n", synth.getNumActiveVoices());
    }
    std::printf ("native sfizz: %d voices, %.2f ms per second of audio (best of 3), %.0f us per voice-second, RTF %.0f\n",
                 voices, best, best * 1000.0 / voices, 1000.0 / best);
    return 0;
}

int main (int argc, char** argv)
{
    if (argc == 4 && std::strcmp (argv[2], "time") == 0)
        return timeVoices (argv[1], std::atoi (argv[3]));

    if (argc < 5)
    {
        std::fprintf (stderr, "usage: %s <file.sfz> <key> <frames> <out.f32> [block] [rate]\n", argv[0]);
        return 2;
    }
    const int key = std::atoi (argv[2]), frames = std::atoi (argv[3]);
    const int block = argc > 5 ? std::atoi (argv[5]) : 512;
    const float rate = argc > 6 ? (float) std::atof (argv[6]) : 48000.0f;

    sfz::Sfizz synth;
    synth.setSampleRate (rate);
    synth.setSamplesPerBlock (block);
    // Synchronous file loading, the offline equivalent of the wasm build's forced RAM loading.
    freewheelAtLiveQuality (synth);
    if (! synth.loadSfzFile (argv[1]) || synth.getNumRegions() == 0)
    {
        std::fprintf (stderr, "load failed\n");
        return 1;
    }

    std::vector<float> l (block), r (block), out;
    out.reserve ((size_t) frames);
    // The velocity SamplerEngine passes for the host's 0.8: JUCE's 7-bit conversion, round(0.8 * 127).
    synth.noteOn (0, key, 102);
    for (int done = 0; done < frames; done += block)
    {
        const int n = frames - done < block ? frames - done : block;
        float* ch[2] = { l.data(), r.data() };
        synth.renderBlock (ch, (size_t) n);
        out.insert (out.end(), l.begin(), l.begin() + n);
    }

    auto* f = std::fopen (argv[4], "wb");
    if (f == nullptr) return 1;
    std::fwrite (out.data(), sizeof (float), out.size(), f);
    std::fclose (f);
    std::printf ("native sfizz: key %d, %d frames, block %d -> %s\n", key, frames, block, argv[4]);
    return 0;
}
