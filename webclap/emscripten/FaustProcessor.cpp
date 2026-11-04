// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
//
// synthis-et4.17: the Synth.is plugin as a WCLAP, FAUST SPIKE. One real Faust artefact (the compiled .wasm that
// faustwasm made, its DSP JSON and its Gate 0 plan) is embedded in the module at build time, and the plugin's own
// FaustEngine (src/FaustEngine.cpp, built with KROMOSYNTH_WASM3=1) runs it on wasm3, which is compiled into this
// module. So this is wasm-in-wasm: the host's wasm runtime runs wasm3, and wasm3 interprets the artefact.
//
// Two test controls, not genome parameters (those stay out of the spike):
//   Voices       FaustEngine's voice count (1..32), for the real-time voice measurement.
//   Measure      when switched on, runs FaustEngine::measure() once on one voice (the load-time peak and single-voice
//                real-time factor the native plugin takes, rendered inside this module) and prints it to stdout. Its
//                rtf needs a host whose WASI clock is real time (wclap-host-node: clock 'real').
//   Replay plan  when on, a note-on replays the artefact's .plan instead of playing the note: the parameter writes
//                at the plan's sample positions, one voice, exactly what faust-probe's runNullTest does natively
//                (tools/faust-probe/main.cpp). With a host block size equal to the plan's (128), the output is
//                comparable sample for sample with the platform reference <stem>.wasmtime-archive.f32.
// See docs/PLUGIN_AS_WCLAP_FAUST_SPIKE_2026-11-04.md.

#include <juce_webclap/juce_webclap_clap.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <clap/plugin-features.h>

#include "FaustEngine.h"
#include "synthis_faust_artefact.h"

#include <cstdio>
#include <vector>

namespace
{
struct PlanEvent { int atSample = 0, byteOffset = 0; float value = 0.0f; };

struct Plan
{
    int sampleRate = 0, blockSize = 0;
    double seconds = 0.0;
    std::vector<PlanEvent> events;
};

// The same format faust-probe's readPlan reads.
Plan parsePlan (const juce::String& text)
{
    Plan plan;
    juce::StringArray t;
    t.addTokens (text, " \t\r\n", "");
    t.removeEmptyStrings();
    for (int i = 0; i < t.size();)
    {
        const auto key = t[i++];
        auto next = [&] { return i < t.size() ? t[i++] : juce::String(); };
        if      (key == "sampleRate") plan.sampleRate = next().getIntValue();
        else if (key == "blockSize")  plan.blockSize  = next().getIntValue();
        else if (key == "seconds")    plan.seconds    = next().getDoubleValue();
        else if (key == "event")
        {
            PlanEvent e;
            e.atSample = next().getIntValue();
            e.byteOffset = next().getIntValue();
            e.value = (float) next().getDoubleValue();
            next(); // label
            plan.events.push_back (e);
        }
        else next(); // structBytes, inputs, outputs: FaustEngine reads them from the JSON
    }
    return plan;
}

class FaustProcessor : public juce::AudioProcessor
{
public:
    FaustProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (voices = new juce::AudioParameterInt ({ "voices", 1 }, "Voices", 1, 32, 8));
        addParameter (replay = new juce::AudioParameterBool ({ "replay", 1 }, "Replay plan", false));
        addParameter (measure = new juce::AudioParameterBool ({ "measure", 1 }, "Measure", false));
        plan = parsePlan (juce::String::fromUTF8 ((const char*) synthis_artefact::kPlan));
    }

    const juce::String getName() const override { return "Synth.is Faust spike"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay (double rate, int maxBlock) override
    {
        currentVoices = voices->get();
        const auto r = engine.loadFromMemory (synthis_artefact::kWasm, synthis_artefact::kWasmSize,
                                              juce::String::fromUTF8 ((const char*) synthis_artefact::kJson), rate, maxBlock, currentVoices);
        // wclap-host-node collects stdout (WASI fd_write) in host.stdout.
        std::printf ("faust: %s %s, %d voices, %d Hz, block %d, struct %d B, gate=%d\n",
                     synthis_artefact::kName, r.ok ? "loaded" : r.detail.toRawUTF8(), r.voices, (int) rate,
                     maxBlock, r.structBytes, (int) r.hasGate);
        std::fflush (stdout);
        replaying = false;
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (measure->get() != measured)
        {
            measured = measure->get();
            if (measured)
            {
                // ONE voice, as the plugin measures (PluginProcessor loads one voice, measures, then sets the cap):
                // measure() drives every voice, so with the 8 loaded here its peaks would read 8x.
                engine.setVoiceCount (1);
                currentVoices = 1;
                const auto m = engine.measure();
                std::printf ("measure: rtf %.2f (single voice, free-running), capAt4x %d, peak %.6f, peaks c3/a4/a5 "
                             "%.6f/%.6f/%.6f\n", m.rtf, m.capAt4x, m.peak, m.peakC3, m.peakA4, m.peakA5);
                std::fflush (stdout);
            }
        }

        if (replay->get())
        {
            for (const auto m : midi)
                if (m.getMessage().isNoteOn())
                {
                    // A fresh voice, as runNullTest's fresh load: setVoiceCount re-instantiates.
                    engine.setVoiceCount (1);
                    currentVoices = 1;
                    replaying = true;
                    replayPos = 0;
                    nextEvent = 0;
                }

            if (! replaying) { buffer.clear(); return; }

            // Events land on block boundaries, as in runNullTest.
            while (nextEvent < plan.events.size() && plan.events[nextEvent].atSample <= replayPos)
            {
                engine.setParameter (plan.events[nextEvent].byteOffset, plan.events[nextEvent].value);
                ++nextEvent;
            }
            engine.process (buffer, juce::MidiBuffer());
            replayPos += buffer.getNumSamples();
            return;
        }

        replaying = false;
        if (voices->get() != currentVoices && engine.setVoiceCount (voices->get()))
            currentVoices = voices->get();
        engine.process (buffer, midi);
    }

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    FaustEngine engine;
    Plan plan;
    juce::AudioParameterInt* voices = nullptr;
    juce::AudioParameterBool* replay = nullptr;
    juce::AudioParameterBool* measure = nullptr;
    bool measured = false;
    int currentVoices = 0;
    bool replaying = false;
    int replayPos = 0;
    size_t nextEvent = 0;
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FaustProcessor(); }

const juce::webclap::ClapPluginInfo& juce::webclap::getClapPluginInfo()
{
    static const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr };
    static const ClapPluginInfo info {
        "is.synth.plugin.faust-spike", "Synth.is Faust spike", "Synth.is", "https://synth.is", "0.0.1",
        "Spike: one embedded Faust artefact run by FaustEngine on wasm3 inside the module", features,
        "/ui/index.html", 600, 400
    };
    return info;
}
