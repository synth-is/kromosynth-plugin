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

// Overridable, because the host is not the probe: GarageBand reported
// sampleRate=44100 blockSize=1024 while every measurement so far was taken at
// 48000/512.
double kSampleRate = 48000.0;
int    kBlockSize = 512;

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

// A realistic played passage rather than one note: several keys pressed and
// released with overlap, the way Musical Typing produces them. The engine
// handling ONE note correctly says nothing about eight voices being allocated,
// released, stolen and retriggered against each other.
int runSequence (FaustEngine& engine, int voices)
{
    juce::AudioBuffer<float> buffer { 2, kBlockSize };
    const int blocksPerStep = juce::jmax (1, (int) (0.15 * kSampleRate) / kBlockSize);
    const int notes[] = { 60, 64, 67, 72, 55, 62, 69, 59, 65, 57 };

    float loudest = 0.0f;

    for (int n = 0; n < (int) (sizeof (notes) / sizeof (notes[0])); ++n)
    {
        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, notes[n], (juce::uint8) 100), 0);
        for (int b = 0; b < blocksPerStep; ++b)
        {
            engine.process (buffer, b == 0 ? on : juce::MidiBuffer{});
            loudest = juce::jmax (loudest, buffer.getMagnitude (0, buffer.getNumSamples()));
        }

        // Note-off deliberately NOT on a block boundary.
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, notes[n]), kBlockSize / 3);
        for (int b = 0; b < blocksPerStep; ++b)
        {
            engine.process (buffer, b == 0 ? off : juce::MidiBuffer{});
            loudest = juce::jmax (loudest, buffer.getMagnitude (0, buffer.getNumSamples()));
        }
    }

    // Long enough to cover grace (1.5 s) plus fade (0.2 s) with room to spare.
    float tail = 0.0f;
    const int settleBlocks = (int) (4.0 * kSampleRate) / kBlockSize;
    for (int b = 0; b < settleBlocks; ++b)
    {
        engine.process (buffer, juce::MidiBuffer{});
        tail = buffer.getMagnitude (0, buffer.getNumSamples());
    }

    std::printf ("\nsequence   10 notes, %d voices, %g Hz, %d-frame blocks\n",
                 voices, kSampleRate, kBlockSize);
    std::printf ("loudest    %8.6f\n", loudest);
    std::printf ("after 4 s  %8.6f   %s\n", tail,
                 tail > 1.0e-5f ? "STUCK — something is still sounding" : "silent");
    std::printf ("forced     %8d\n", engine.forcedStops());

    return tail > 1.0e-5f ? 1 : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// The three numbers an artefact should carry and currently carries none of.
//
//   rtf          single-voice real-time factor -> a per-platform voice cap
//   trim         gain to bring this genome to a usable level
//   articulation whether releasing the gate stops the sound
//
// All three are one render to compute at export and none is recoverable
// afterwards. EXPERIMENT_01_3_DESKTOP_RESULT §2.7 proposes the first;
// ELITE_PLAYABILITY_2026-08-29 documents a 64 dB level spread that demands the
// second; ELITE_ARTICULATION_2026-09-01 measures 16 of 33 genomes needing the
// third.
//
// ── Two disciplines carried in from earlier sessions ────────────────────────
//
// 1. THE GATE IS DRIVEN BY PARAMETER, NOT BY noteOn. FaustEngine's forced
//    release only engages on note-driven voices, so measuring through MIDI
//    would measure the plugin's own intervention rather than the genome. This
//    path is the same one the 33/33 null test uses.
//
// 2. PEAK IS PROBED AT SEVERAL PITCHES. A single-pitch peak measurement
//    produced a 38 dB error on one genome in the composing-app work. Level is
//    not pitch-independent and must not be assumed to be.
//
// Gain is probed at 1.0 rather than the genome's init, because the plugin maps
// velocity to gain and full velocity is what a user will actually send.
// ─────────────────────────────────────────────────────────────────────────────

int runMeasure (const juce::File& wasmFile, const juce::File& jsonFile,
                const juce::String& name)
{
    // Honours --rate/--block so the host's exact conditions can be reproduced.
    // GarageBand reported 44100/1024 while every measurement had been taken at
    // 48000/128, and that difference had to be ruled out by measuring it.
    const double kRate = kSampleRate;
    const int    kBlock = kBlockSize;
    constexpr float  kTargetPeak = 0.25f;      // as kromosynth-composing's trimForLevel
    const int pitches[] = { 48, 69, 81 };      // c3, a4, a5

    FaustEngine engine;
    const auto load = engine.load (wasmFile, jsonFile, kRate, kBlock, 1);
    if (! load.ok)
    {
        std::printf ("{\"name\":\"%s\",\"ok\":false,\"detail\":\"%s\"}\n",
                     name.toRawUTF8(), load.detail.toRawUTF8());
        return 1;
    }

    int idxFreq = -1, idxGain = -1, idxGate = -1;
    for (const auto& prm : engine.parameters())
    {
        if (prm.label == "freq") idxFreq = prm.byteOffset;
        if (prm.label == "gain") idxGain = prm.byteOffset;
        if (prm.label == "gate") idxGate = prm.byteOffset;
    }

    juce::AudioBuffer<float> buffer { 1, kBlock };
    const juce::MidiBuffer noMidi;

    const int holdBlocks  = (int) (2.0 * kRate) / kBlock;
    const int afterBlocks = (int) (3.0 * kRate) / kBlock;

    float peakOverall = 0.0f, peakByPitch[3] = { 0, 0, 0 };
    float tailRel = 0.0f;          // level 2-3 s after gate-off, vs this render's peak
    double decayToSilenceS = -1.0; // -1 means it never got there

    for (int pi = 0; pi < 3; ++pi)
    {
        engine.panic();
        // DOUBLE, narrowed once at the parameter. This was float throughout --
        // std::pow (2.0f, -1.75f) -- and that made the frequency itself differ
        // from the same calculation done in double by ~1e-5 relative.
        //
        // Invisible while this was the only measurer. It surfaced the moment
        // render-socket's port was compared against it: peakC3 disagreed by
        // 9.8e-6 while peakA4 and peakA5 agreed exactly, because a4 and a5 have
        // exponents of 0 and 1 where pow is exact and c3 has -1.75 where it is
        // not. A disagreement in the fifth decimal that is really a
        // disagreement about what note was played.
        //
        // The DSP parameter is float32 either way, so this narrows once at the
        // end rather than accumulating in float. peakC3 figures published in
        // ARTEFACT_NUMBERS_2026-09-01 predate this and differ in the 5th decimal.
        if (idxFreq >= 0) engine.setParameter (idxFreq,
            (float) (440.0 * std::pow (2.0, (pitches[pi] - 69) / 12.0)));
        if (idxGain >= 0) engine.setParameter (idxGain, 1.0f);
        if (idxGate >= 0) engine.setParameter (idxGate, 1.0f);

        float peakHere = 0.0f;
        for (int b = 0; b < holdBlocks; ++b)
        {
            engine.process (buffer, noMidi);
            peakHere = juce::jmax (peakHere, buffer.getMagnitude (0, kBlock));
        }

        if (idxGate >= 0) engine.setParameter (idxGate, 0.0f);

        float lastSecond = 0.0f;
        int quietAt = -1;
        for (int b = 0; b < afterBlocks; ++b)
        {
            engine.process (buffer, noMidi);
            const float m = buffer.getMagnitude (0, kBlock);
            peakHere = juce::jmax (peakHere, m);

            if (quietAt < 0 && peakHere > 0.0f && m < peakHere * 0.001f)   // -60 dB
                quietAt = b;

            if (b >= afterBlocks - (int) (1.0 * kRate) / kBlock)
                lastSecond = juce::jmax (lastSecond, m);
        }

        peakByPitch[pi] = peakHere;
        peakOverall = juce::jmax (peakOverall, peakHere);

        // Reported for the loudest pitch, which is the one that decides the trim.
        if (peakHere >= peakOverall)
        {
            tailRel = peakHere > 0.0f ? lastSecond / peakHere : 0.0f;
            decayToSilenceS = quietAt < 0 ? -1.0 : (double) quietAt * kBlock / kRate;
        }
    }

    // ── RTF, free-running, one voice ────────────────────────────────────────
    // Labelled free-running deliberately. d3321de measured that holding a real
    // block cadence costs 2.3-2.9x more than rendering flat out, so this is a
    // relative figure for ranking genomes, NOT a claim about headroom in a DAW.
    engine.panic();
    if (idxFreq >= 0) engine.setParameter (idxFreq, 440.0f);
    if (idxGain >= 0) engine.setParameter (idxGain, 1.0f);
    if (idxGate >= 0) engine.setParameter (idxGate, 1.0f);

    const int rtfBlocks = (int) (5.0 * kRate) / kBlock;
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (int b = 0; b < rtfBlocks; ++b) engine.process (buffer, noMidi);
    const double elapsed = juce::Time::highResolutionTicksToSeconds (
                               juce::Time::getHighResolutionTicks() - t0);
    const double rtf = elapsed > 0.0 ? 5.0 / elapsed : 0.0;

    // Gate 0 §5 validated single-voice RTF as an exact predictor of polyphony,
    // 30/30, using a 1.05 per-voice penalty.
    const auto capAt = [rtf] (double headroom)
    {
        int cap = 0;
        for (int v = 1; v <= 32; v *= 2)
            if (rtf / (v * 1.05) >= headroom) cap = v;
        return cap;
    };

    const float trim = peakOverall > 0.0f ? kTargetPeak / peakOverall : 1.0f;
    const char* articulation = tailRel < 0.001f ? "silent"
                             : tailRel < 0.032f ? "quiet" : "drone";

    std::printf (
        "{\"name\":\"%s\",\"ok\":true,\"dsp\":\"%s\",\"structBytes\":%d,"
        "\"rtf\":%.2f,\"rtfMode\":\"free-running\","
        "\"capAt4x\":%d,\"capAt2x\":%d,"
        "\"peak\":%.6f,\"peakC3\":%.6f,\"peakA4\":%.6f,\"peakA5\":%.6f,"
        "\"pitchSpreadDb\":%.1f,"
        "\"targetPeak\":%.3f,\"trim\":%.6f,\"trimDb\":%.1f,"
        "\"articulation\":\"%s\",\"tailDb\":%.1f,\"decayToSilenceS\":%.3f,"
        "\"hasGate\":%s}\n",
        name.toRawUTF8(), load.dspName.toRawUTF8(), load.structBytes,
        rtf, capAt (4.0), capAt (2.0),
        peakOverall, peakByPitch[0], peakByPitch[1], peakByPitch[2],
        20.0 * std::log10 (juce::jmax (1.0e-9f, peakOverall)
                           / juce::jmax (1.0e-9f, juce::jmin (juce::jmin (peakByPitch[0], peakByPitch[1]), peakByPitch[2]))),
        kTargetPeak, trim, 20.0 * std::log10 (juce::jmax (1.0e-9f, trim)),
        articulation, 20.0 * std::log10 (juce::jmax (1.0e-9f, tailRel)),
        decayToSilenceS, load.hasGate ? "true" : "false");

    return 0;
}

/** Plays notes through the engine and reports sample-to-sample discontinuities.

    A click IS a discontinuity: one sample far from the last. The browser
    preview cannot produce one, because it renders a single note offline from a
    fresh processor; the plugin plays live with REUSED voices, and every
    difference between the two lives on that seam.

    So rather than reasoning about which seam it is, this drives the seams --
    note from silence, note switch, retrigger on the same voice, overlap -- and
    prints where the jumps actually are. The largest jump per phase is the
    answer; guessing at causes is what this replaces.
*/
static int runClickTest (const juce::File& wasmFile, const juce::File& jsonFile, int voices)
{
    FaustEngine engine;

    const auto load = engine.load (wasmFile, jsonFile, kSampleRate, kBlockSize, voices);
    if (! load.ok)
    {
        std::printf ("load failed: %s\n", load.detail.toRawUTF8());
        return 1;
    }

    // Exactly the plugin's playback configuration. Without the compensation and
    // the ceiling this would measure a different engine than the one that
    // clicks.
    const auto m = engine.measure();
    engine.setPitchCompensation (m.pitchPeaks, m.peakA4 > 0.0f ? m.peakA4 : m.peak);
    engine.setOutputCeiling (true);
    engine.setOutputGain (m.peakA4 > 1.0e-6f ? juce::jmin (1.0f, 1.0f / m.peakA4) : 1.0f);
    engine.setVoiceCount (voices);

    juce::AudioBuffer<float> buf { 1, kBlockSize };

    float previous = 0.0f;
    bool havePrevious = false;

    const auto runBlocks = [&] (const char* label, int blocks, const juce::MidiBuffer& firstBlockMidi)
    {
        // TWO numbers, because one cannot tell a click from a waveform.
        //
        // A sawtooth at 0.4 peak jumps 0.8 at every cycle reset, legitimately,
        // and the first version of this reported that as "audible". What marks
        // a CLICK is a jump at the note BOUNDARY that the rest of the note does
        // not also produce -- so the first 5 ms after the event are measured
        // separately from everything after them.
        const int boundarySamples = (int) (0.005 * kSampleRate);

        float atBoundary = 0.0f, inSteadyState = 0.0f;
        int sampleIndex = 0;

        for (int b = 0; b < blocks; ++b)
        {
            const juce::MidiBuffer& midi = (b == 0) ? firstBlockMidi : juce::MidiBuffer{};
            buf.clear();
            engine.process (buf, midi);

            const auto* d = buf.getReadPointer (0);
            for (int i = 0; i < kBlockSize; ++i)
            {
                if (havePrevious)
                {
                    const float jump = std::abs (d[i] - previous);
                    float& into = sampleIndex < boundarySamples ? atBoundary : inSteadyState;
                    if (jump > into) into = jump;
                }
                previous = d[i];
                havePrevious = true;
                ++sampleIndex;
            }
        }

        // A boundary jump well past what the note itself produces is the thing
        // being looked for. The margin is deliberately generous: a note that
        // starts abruptly by design is not a defect.
        const bool click = atBoundary > 0.02f && atBoundary > inSteadyState * 2.5f;

        std::printf ("  %-26s boundary %.5f   steady %.5f%s\n",
                     label, atBoundary, inSteadyState, click ? "   <-- CLICK" : "");
        return atBoundary;
    };

    const auto noteOnAt = [] (int note)
    {
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        return mb;
    };
    const auto noteOffAt = [] (int note)
    {
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        return mb;
    };

    const int blocksPerSecond = (int) (kSampleRate / kBlockSize);

    std::printf ("clicks     rate=%.0f block=%d voices=%d\n", kSampleRate, kBlockSize, voices);
    std::printf ("           boundary = largest jump in the first 5 ms after the event\n");
    std::printf ("           steady   = largest jump over the rest of the phase\n\n");

    // Silence first, so the baseline is the engine doing nothing.
    runBlocks ("idle", 4, juce::MidiBuffer{});

    // 1. A note from a genuinely fresh engine.
    runBlocks ("first note (fresh)", blocksPerSecond, noteOnAt (69));
    runBlocks ("  ... released", blocksPerSecond * 2, noteOffAt (69));

    // 2. The SAME note again. The voice is reused, and unless something reset
    //    it, the DSP struct still holds the first note's filter and delay state.
    runBlocks ("same note again (reuse)", blocksPerSecond, noteOnAt (69));
    runBlocks ("  ... released", blocksPerSecond * 2, noteOffAt (69));

    // 3. A DIFFERENT pitch on the reused voice. This is where the per-note
    //    correction changes as well as the frequency.
    runBlocks ("different pitch (reuse)", blocksPerSecond, noteOnAt (48));
    runBlocks ("  ... released", blocksPerSecond * 2, noteOffAt (48));

    // 4. Switching while the previous note is still sounding.
    runBlocks ("note held", blocksPerSecond / 4, noteOnAt (60));
    runBlocks ("switch while sounding", blocksPerSecond / 4, noteOnAt (72));
    runBlocks ("switch again", blocksPerSecond / 4, noteOnAt (55));

    // 5. More notes than voices, forcing a steal of something still audible.
    for (int i = 0; i < voices + 2; ++i)
        runBlocks (i == 0 ? "stacking notes" : "  ... another",
                   blocksPerSecond / 8, noteOnAt (50 + i * 2));

    // 6. PLAYING. Short notes with short gaps, moving around -- the case the
    //    phases above miss entirely, because they release for two full seconds
    //    and let every voice fall silent first.
    //
    //    This is what a person does, and it is where the crackle was reported.
    //    A released voice is marked free IMMEDIATELY while it is still ringing,
    //    so at this tempo the allocator is choosing between voices that are all
    //    still sounding.
    std::printf ("\n");
    engine.panic();
    havePrevious = false;

    const int shortNote = juce::jmax (1, blocksPerSecond / 6);   // ~170 ms
    const int shortGap  = juce::jmax (1, blocksPerSecond / 12);  // ~85 ms
    const int melody[] = { 60, 64, 67, 64, 60, 67, 72, 67 };

    for (int i = 0; i < (int) (sizeof (melody) / sizeof (melody[0])); ++i)
    {
        char label[64];
        std::snprintf (label, sizeof (label), "playing: note %d", melody[i]);
        runBlocks (label, shortNote, noteOnAt (melody[i]));

        char offLabel[64];
        std::snprintf (offLabel, sizeof (offLabel), "  ... off");
        runBlocks (offLabel, shortGap, noteOffAt (melody[i]));
    }

    std::printf ("\n");
    return 0;
}

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

    for (int i = 3; i + 1 < argc; ++i)
    {
        if (juce::String (argv[i]) == "--rate")  kSampleRate = std::atof (argv[i + 1]);
        if (juce::String (argv[i]) == "--block") kBlockSize  = std::atoi (argv[i + 1]);
    }

    int planArg = -1, refArg = -1;
    for (int i = 3; i + 1 < argc; ++i)
    {
        if (juce::String (argv[i]) == "--plan") planArg = i + 1;
        if (juce::String (argv[i]) == "--ref")  refArg  = i + 1;
    }

    for (int i = 3; i < argc; ++i)
        if (juce::String (argv[i]) == "--clicks")
            return runClickTest (wasmFile, jsonFile, voices);

    for (int i = 3; i < argc; ++i)
        if (juce::String (argv[i]) == "--measure")
            return runMeasure (wasmFile, jsonFile, wasmFile.getFileNameWithoutExtension());

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

    for (int i = 3; i < argc; ++i)
        if (juce::String (argv[i]) == "--seq")
            return runSequence (engine, voices);

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
