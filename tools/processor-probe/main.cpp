// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// processor-probe — renders a fixed MIDI arpeggio through the plugin's own
// KromosynthAudioProcessor, with no host, no editor and no audio device.
//
// DEVELOPER TOOL, never shipped.
//
//     processor-probe <instrument-id> <out.wav>
//     KROMOSYNTH_PROBE_LIBRARY=<dir containing Instruments/<id>/...>
//
// Prints key=value lines on stdout; scripts/processor-diff.sh reads them. The
// first line it checks is `loaded=1`: an unloaded instrument renders silence,
// and a silent WAV looks exactly like a plugin that never loaded (the same
// reason make_projects.py insists on reading -splashlog).
//
// What is driven, deliberately through the same calls a host makes:
//   prepareToPlay -> loadInstrument (message thread, polled until measured)
//   -> processBlock with a MidiBuffer per block -> a parameter write through the
//   AudioProcessorParameter interface mid-render.

#include "../../src/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kSeconds = 14.0;

struct Event { double at; bool on; int note; int velocity; };

// A fixed script. Every number here is part of what two builds are compared on,
// so changing any of them invalidates a recorded reference.
std::vector<Event> script()
{
    std::vector<Event> e;
    const int arp[] = { 60, 64, 67, 72, 67, 64 };

    // 1. Arpeggio: 24 eighth notes at 0.25 s, each held 0.4 s so successive
    //    notes overlap and the release tails stack. Velocity cycles.
    for (int i = 0; i < 24; ++i)
    {
        const double t = 0.25 * i;
        const int n = arp[i % 6] + (i >= 12 ? 12 : 0);
        const int v = 40 + (i % 4) * 29;
        e.push_back ({ t, true, n, v });
        e.push_back ({ t + 0.4, false, n, 0 });
    }

    // 2. A held cluster, past the voice cap, so
    //    stealing shows up in the counts rather than depending on the host.
    //    40 notes against a cap of 32 at the time of writing.
    for (int i = 0; i < 40; ++i)
        e.push_back ({ 7.0 + 0.005 * i, true, 36 + i, 100 });
    for (int i = 0; i < 40; ++i)
        e.push_back ({ 9.0, false, 36 + i, 0 });

    // 3. A re-trigger of one key with no note-off between.
    e.push_back ({ 10.0, true, 60, 90 });
    e.push_back ({ 10.3, true, 60, 90 });
    e.push_back ({ 10.8, false, 60, 0 });
    return e;
}

juce::RangedAudioParameter* findParam (juce::AudioProcessor& p, const char* id)
{
    for (auto* q : p.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (q))
            if (withId->getParameterID() == id)
                return withId;
    return nullptr;
}
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: processor-probe <instrument-id> <out.wav>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String id (argv[1]);

    // Heap, so it can be destroyed before the initialiser.
    auto processor = std::make_unique<KromosynthAudioProcessor>();
    processor->setPlayConfigDetails (0, 2, kRate, kBlock);
    processor->prepareToPlay (kRate, kBlock);

    const auto outcome = processor->loadInstrument (id);
    std::printf ("loaded=%d\ndetail=%s\n", outcome.ok ? 1 : 0, outcome.detail.toRawUTF8());

    // The measurement runs on a background thread and the processor stays
    // suspended until it finishes.
    const auto deadline = juce::Time::getMillisecondCounter() + 120000;
    while (processor->isMeasuring() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    // currentInstrument(), not lastLoadOutcome(): a load served from the
    // measurement cache never writes lastOutcome_.
    const bool loadedOk = ! processor->isMeasuring() && processor->currentInstrument().ok;
    std::printf ("measured=%d\nload_ok=%d\n", processor->isMeasuring() ? 0 : 1, loadedOk ? 1 : 0);
    if (! loadedOk)
        return 1;

    // What a HOST sees of the macro pool, read through the AudioProcessorParameter
    // interface a host uses -- name, text and default as they stand after the
    // load, not what bindMacros believes it set. Printed on every run, so a
    // build without the pool simply has no `macros_bound` line to compare.
    int macrosBound = 0;
    for (int i = 0; i < KromosynthAudioProcessor::kNumMacros; ++i)
    {
        auto* param = findParam (*processor, ("macro" + juce::String (i + 1)).toRawUTF8());
        if (param == nullptr) continue;

        const auto b = processor->macroBinding (i);
        if (b.label.isNotEmpty()) ++macrosBound;

        std::printf ("macro%d=name:%s|bound:%s|range:%g..%g|init:%g|host_default_text:%s|host_value_text:%s\n",
                     i + 1, param->getName (64).toRawUTF8(), b.label.isNotEmpty() ? "yes" : "no",
                     (double) b.minimum, (double) b.maximum, (double) b.init,
                     param->getText (param->getDefaultValue(), 32).toRawUTF8(),
                     param->getText (param->getValue(), 32).toRawUTF8());
    }
    std::printf ("macros_bound=%d\n", macrosBound);

    // KROMOSYNTH_PROBE_MACRO="1=0.5,3=0.2": move macros through the PARAMETER
    // path a host automation write takes (a 0..1 value), the real value given
    // here in the slider's own units and converted the way the host's text box
    // would, via getValueForText. Applied before the first block, so the whole
    // render hears it.
    if (const auto spec = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_MACRO", {});
        spec.isNotEmpty())
    {
        for (const auto& item : juce::StringArray::fromTokens (spec, ",", ""))
        {
            const int slot = item.upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const auto text = item.fromFirstOccurrenceOf ("=", false, false);
            auto* param = findParam (*processor, ("macro" + juce::String (slot)).toRawUTF8());
            if (param == nullptr) { std::printf ("macro_set_%d=missing\n", slot); continue; }

            param->setValueNotifyingHost (param->getValueForText (text));
            std::printf ("macro_set_%d=%s -> normalised %.6f -> text %s\n", slot, text.toRawUTF8(),
                         (double) param->getValue(), param->getText (param->getValue(), 32).toRawUTF8());
        }
    }

    // KROMOSYNTH_PROBE_STATE_ROUNDTRIP=1: save this processor's state and restore
    // it into a SECOND processor, the way a host reopening a session does, then
    // print what the second one holds. The instrument arrives by the
    // setStateInformation route (an async load), not by a loadInstrument call.
    if (juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_STATE_ROUNDTRIP", {}).isNotEmpty())
    {
        juce::MemoryBlock state;
        processor->getStateInformation (state);

        auto second = std::make_unique<KromosynthAudioProcessor>();
        second->setPlayConfigDetails (0, 2, kRate, kBlock);
        second->prepareToPlay (kRate, kBlock);
        second->setStateInformation (state.getData(), (int) state.getSize());

        const auto until = juce::Time::getMillisecondCounter() + 120000;
        do { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); }
        while ((second->isMeasuring() || ! second->currentInstrument().ok)
               && juce::Time::getMillisecondCounter() < until);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        std::printf ("roundtrip_state_bytes=%d\nroundtrip_loaded=%d\n", (int) state.getSize(),
                     second->currentInstrument().ok ? 1 : 0);
        for (int i = 0; i < KromosynthAudioProcessor::kNumMacros; ++i)
        {
            const auto a = processor->macroBinding (i);
            const auto b = second->macroBinding (i);
            if (a.label.isEmpty() && b.label.isEmpty()) continue;
            std::printf ("roundtrip_macro%d=%s|%g|restored:%s|%g\n", i + 1, a.label.toRawUTF8(),
                         (double) a.value, b.label.toRawUTF8(), (double) b.value);
        }
        second.reset();
    }

    // KROMOSYNTH_PROBE_MACRO_RESET_AT=<seconds>: put every macro set above back to
    // its default part-way through, with the note still held. Exists for the one
    // path a fixed arpeggio cannot isolate -- a knob returned to its default must
    // be written back, and voices going idle re-init anyway, so only a note held
    // across the reset can tell the two apart.
    std::vector<juce::RangedAudioParameter*> movedMacros;
    if (const auto spec = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_MACRO", {});
        spec.isNotEmpty())
        for (const auto& item : juce::StringArray::fromTokens (spec, ",", ""))
            if (auto* param = findParam (*processor,
                    ("macro" + juce::String (item.upToFirstOccurrenceOf ("=", false, false).getIntValue())).toRawUTF8()))
                movedMacros.push_back (param);

    const auto resetAtText = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_MACRO_RESET_AT", {});
    const double resetAt = resetAtText.isNotEmpty() ? resetAtText.getDoubleValue() : -1.0;
    bool macrosReset = false;

    // KROMOSYNTH_PROBE_SCRIPT=hold: one note, held for 6 s, nothing else.
    // Not the reference script, so never compared against a recorded one.
    auto events = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_SCRIPT", {}) == "hold"
                      ? std::vector<Event> { { 0.0, true, 60, 100 }, { 6.0, false, 60, 0 } }
                      : script();
    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.at < b.at; });

    const int totalBlocks = (int) std::ceil (kSeconds * kRate / kBlock);
    juce::AudioBuffer<float> out (2, totalBlocks * kBlock);
    out.clear();

    juce::AudioBuffer<float> block (2, kBlock);
    juce::MidiBuffer midi;
    size_t next = 0;
    int maxActive = 0;
    bool paramWritten = false;

    for (int b = 0; b < totalBlocks; ++b)
    {
        midi.clear();
        const auto start = (juce::int64) b * kBlock;

        while (next < events.size() && (juce::int64) std::llround (events[next].at * kRate) < start + kBlock)
        {
            const auto& ev = events[next++];
            const int offset = juce::jmax (0, (int) ((juce::int64) std::llround (ev.at * kRate) - start));
            midi.addEvent (ev.on ? juce::MidiMessage::noteOn (1, ev.note, (juce::uint8) ev.velocity)
                                 : juce::MidiMessage::noteOff (1, ev.note), offset);
        }

        // The parameter path: a host automation write, once, at 5 s.
        if (! paramWritten && start >= (juce::int64) (5.0 * kRate))
        {
            if (auto* release = findParam (*processor, "release"))
                release->setValueNotifyingHost (release->convertTo0to1 (0.35f));
            std::printf ("param_release_found=%d\n", findParam (*processor, "release") != nullptr ? 1 : 0);
            paramWritten = true;
        }

        if (! macrosReset && resetAt >= 0.0 && start >= (juce::int64) (resetAt * kRate))
        {
            for (auto* param : movedMacros)
                param->setValueNotifyingHost (param->getDefaultValue());
            std::printf ("macros_reset_at=%.3f\n", resetAt);
            macrosReset = true;
        }

        block.clear();
        processor->processBlock (block, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, b * kBlock, block, c, 0, kBlock);

        maxActive = juce::jmax (maxActive, processor->engineState().activeVoices);
    }

    const auto st = processor->engineState();
    std::printf ("note_ons=%d\nnote_offs=%d\nstarts_idle=%d\nstarts_reused=%d\nstarts_stolen=%d\n"
                 "force_stopped=%d\nmax_active_voices=%d\nvoice_cap=%d\nrender_peak=%.6f\nblocks=%d\n",
                 st.notesOn, st.notesOff, st.startsIdle, st.startsReused, st.startsStolen,
                 st.forcedStops, maxActive, st.voiceCap, (double) st.renderPeak, totalBlocks);

    juce::File wav (argv[2]);
    wav.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (new juce::FileOutputStream (wav));
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate (kRate)
                                                      .withNumChannels (2)
                                                      .withBitsPerSample (32));
    if (writer == nullptr)
    {
        std::fprintf (stderr, "could not write %s\n", argv[2]);
        return 2;
    }
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    writer.reset();

    processor.reset();
    return 0;
}
