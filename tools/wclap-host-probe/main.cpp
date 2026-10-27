// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// wclap-host-probe — synthis-et4.3. Drives KromosynthAudioProcessor with a hosted WCLAP, no host, no
// editor, no audio device. DEVELOPER TOOL, never shipped.
//
//     wclap-host-probe <instrument-id>
//     KROMOSYNTH_PROBE_LIBRARY=<dir holding <instrument-id>/instrument.wclap/module.wasm>
//
// Prints KEY=VALUE lines; exit 0 only if every CHECK_ passed.
#include "../../src/PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Render { juce::AudioBuffer<float> audio; double wallSeconds = 0; };

double rms (const juce::AudioBuffer<float>& b, int from, int to)
{
    double s = 0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i) { const double x = b.getSample (c, i); s += x * x; ++n; }
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

// `notes` held for the whole render, all started in block 0, through processBlock's MidiBuffer.
Render play (KromosynthAudioProcessor& p, const std::vector<int>& notes, double seconds)
{
    const int blocks = (int) std::ceil (seconds * kRate / kBlock);
    Render r;
    r.audio.setSize (2, blocks * kBlock);
    r.audio.clear();
    juce::AudioBuffer<float> block (2, kBlock);
    juce::MidiBuffer midi;
    const auto t0 = std::chrono::steady_clock::now();
    double inside = 0;

    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        block.clear();
        const auto a = std::chrono::steady_clock::now();
        p.processBlock (block, midi);
        inside += std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
        for (int c = 0; c < 2; ++c) r.audio.copyFrom (c, b * kBlock, block, c, 0, kBlock);
    }
    (void) t0;
    r.wallSeconds = inside;   // processBlock only: no copying, no clearing
    // release everything so the next render starts from a quiet plugin
    midi.clear();
    for (int c = 0; c < 128; ++c) midi.addEvent (juce::MidiMessage::noteOff (1, c), 0);
    for (int i = 0; i < 200; ++i) { block.clear(); p.processBlock (block, midi); midi.clear(); }
    return r;
}

bool pumpUntilLoaded (KromosynthAudioProcessor& p, int ms)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! p.currentInstrument().ok && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    return p.currentInstrument().ok;
}

std::unique_ptr<KromosynthAudioProcessor> makeProcessor()
{
    auto p = std::make_unique<KromosynthAudioProcessor>();
    p->setPlayConfigDetails (0, 2, kRate, kBlock);
    p->prepareToPlay (kRate, kBlock);
    return p;
}

// A parameter to move so that "state round-trips" is a claim about the PATCH, not about defaults: the
// first visible, writable, non-stepped parameter, set 80 % of the way along its range (20 % if the
// default already sits there).
bool movedParam (WclapEngine& e, juce::String& name)
{
    for (int i = 0, n = e.numParams(); i < n; ++i)
    {
        WclapEngine::ParamInfo info;
        if (! e.paramInfo (i, info) || info.hidden || info.readOnly || info.stepped) continue;
        const double span = info.maxValue - info.minValue;
        if (span <= 0) continue;
        double v = info.minValue + 0.8 * span;
        if (std::abs (v - info.defaultValue) < 0.05 * span) v = info.minValue + 0.2 * span;
        e.queueParam (info.id, v);
        name = info.name;
        return true;
    }
    return false;
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IOLBF, 0);
    if (argc < 2) { std::fprintf (stderr, "usage: wclap-host-probe <instrument-id>\n"); return 2; }

    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String id (argv[1]);
    int rc = 0;
    auto check = [&] (const char* what, bool ok) { std::printf ("%s=%s\n", what, ok ? "PASS" : "FAIL"); if (! ok) rc = 1; };

    // ── 0. the pinned per-call time limit ────────────────────────────────────────────────────────────
    // A decision, not a default: docs/PLUGIN_WCLAP_HOST_SPIKE.md (CPU section) keeps the limit at
    // 60000 ms — a hung guest taking down the DAW is worse than the ~30 % CPU it costs. The env
    // override exists for measurement; with it unset the pin is 60000.
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_WCLAP_TIME_LIMIT_MS", {});
        if (env.isEmpty())
        {
            const auto limit = WclapEngine::timeLimitMs();
            std::printf ("TIME_LIMIT_MS=%u\n", limit);
            check ("CHECK_TIME_LIMIT_MS_60000", limit == 60000);
        }
        else
            std::printf ("TIME_LIMIT_MS_ENV_OVERRIDE=%s\n", env.toRawUTF8());
    }

    // ── 1. routing: the library must pick the wclap substrate from the folder's file list ───────────
    {
        InstrumentLibrary lib;
        const auto r = lib.resolve (id);
        std::printf ("RESOLVED_KIND=%s\n", r.kind == InstrumentLibrary::Kind::wclap ? "wclap" : "other");
        check ("CHECK_ROUTES_TO_WCLAP", r.kind == InstrumentLibrary::Kind::wclap);
        if (r.kind != InstrumentLibrary::Kind::wclap) return 1;
    }

    // ── 2. load, play a note from the processor's MIDI path ──────────────────────────────────────────
    auto a = makeProcessor();
    const auto tLoad = juce::Time::getMillisecondCounterHiRes();
    const auto outcome = a->loadInstrument (id);
    std::printf ("LOAD_OK=%d\nLOAD_MS=%.0f\nLOAD_DETAIL=%s\n", outcome.ok ? 1 : 0,
                 juce::Time::getMillisecondCounterHiRes() - tLoad, outcome.detail.toRawUTF8());
    if (! outcome.ok) return 1;
    auto* eng = a->wclapEngine();
    check ("CHECK_ENGINE_IS_WCLAP", eng != nullptr);
    if (eng == nullptr) return 1;

    // The first note after activate: this is where a missing settle block shows (Six Sines finding).
    const auto first = play (*a, { 60 }, 2.0);
    const double level = rms (first.audio, (int) (0.1 * kRate), (int) (1.5 * kRate));
    std::printf ("RMS_C4=%g\nPEAK=%g\n", level, (double) first.audio.getMagnitude (0, first.audio.getNumSamples()));
    check ("CHECK_PLAYS_RMS_ABOVE_0.05", level > 0.05);

    // ── 3. clap.state: same instance, with a moved parameter so the state is not the default ────────
    juce::MemoryBlock s0, s1, s2;
    eng->saveState (s0);
    juce::String moved;
    const bool didMove = movedParam (*eng, moved);
    std::printf ("MOVED_PARAM=%s\n", didMove ? moved.toRawUTF8() : "(none found)");
    play (*a, {}, 0.2);   // one block carries the queued param event in
    const bool saved = eng->saveState (s1);
    std::printf ("STATE_BYTES_DEFAULT=%zu\nSTATE_BYTES_MOVED=%zu\n", s0.getSize(), s1.getSize());
    check ("CHECK_STATE_CAPTURES_PATCH", saved && didMove && s1 != s0);
    const bool l1 = eng->loadState (s1.getData(), s1.getSize());
    const bool sv2 = eng->saveState (s2);
    check ("CHECK_STATE_ROUNDTRIP_SAME_INSTANCE", l1 && sv2 && s2 == s1);
    const auto afterLoad = play (*a, { 60 }, 2.0);   // first note after loadState: the settle block
    const double lvlAfter = rms (afterLoad.audio, (int) (0.1 * kRate), (int) (1.5 * kRate));
    std::printf ("RMS_AFTER_LOADSTATE=%g\n", lvlAfter);
    check ("CHECK_PLAYS_AFTER_LOADSTATE", lvlAfter > 0.05);

    // ── 4. the session: processor state -> a FRESH processor (a DAW reopening the project) ──────────
    juce::MemoryBlock session;
    a->getStateInformation (session);
    std::printf ("SESSION_BYTES=%zu\n", session.getSize());

    auto b = makeProcessor();
    b->setStateInformation (session.getData(), (int) session.getSize());
    const bool restored = pumpUntilLoaded (*b, 120000);
    check ("CHECK_FRESH_PROCESSOR_LOADED_FROM_SESSION", restored);
    if (restored && b->wclapEngine() != nullptr)
    {
        juce::MemoryBlock s3;
        const bool got = b->wclapEngine()->saveState (s3);
        std::printf ("STATE_BYTES_FRESH=%zu\n", s3.getSize());
        check ("CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE", got && s3 == s1);
        const auto fresh = play (*b, { 60 }, 2.0);
        const double lvlFresh = rms (fresh.audio, (int) (0.1 * kRate), (int) (1.5 * kRate));
        std::printf ("RMS_FRESH_INSTANCE=%g\n", lvlFresh);
        check ("CHECK_PLAYS_FRESH_INSTANCE", lvlFresh > 0.05);
    }
    else
        check ("CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE", false);
    b.reset();

    // ── 5. CPU: processBlock wall time / audio time, 0 / 1 / 4 / 8 / 16 voices ──────────────────────
    const std::vector<std::vector<int>> chords = {
        {}, { 60 }, { 60, 64, 67, 72 }, { 48, 52, 55, 60, 64, 67, 72, 76 },
        { 36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96 } };
    play (*a, { 60, 64, 67 }, 1.0);   // warm-up, discarded
    for (const auto& chord : chords)
    {
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            const auto r = play (*a, chord, 4.0);
            best = std::min (best, r.wallSeconds / 4.0);
        }
        std::printf ("CPU_RT_FRACTION voices=%zu %g\n", chord.size(), best);
    }

    a.reset();
    return rc;
}
