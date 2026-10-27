// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
//
// synthis-et4.9: the Synth.is plugin as a WCLAP, SKELETON. A juce::AudioProcessor that is the plugin's shape
// (a synth, MIDI in, stereo out, one parameter, state) with a sine voice in place of the engines. No Faust, no
// sampler, no editor: those are the later steps in docs/PLUGIN_AS_WCLAP_2026-10-27.md.
//
// It is NOT KromosynthAudioProcessor. That class owns FaustEngine (Wasmtime/wasm3), SamplerEngine (sfizz),
// WclapEngine (wclap-bridge), the acquisition code and a webview editor, none of which compiles for wasm yet.
// The kit wraps a juce::AudioProcessor through createPluginFilter(), which is all this file provides.

#include <juce_webclap/juce_webclap_clap.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <clap/plugin-features.h>

#include <cmath>

namespace
{
class SkeletonProcessor : public juce::AudioProcessor
{
public:
    SkeletonProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat ({ "gain", 1 }, "Gain", 0.0f, 1.0f, 0.5f));
    }

    const juce::String getName() const override { return "Synth.is skeleton"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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

    void prepareToPlay (double rate, int) override { sampleRate = rate; phase = 0.0; }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        int pos = 0;
        auto render = [&] (int upTo)
        {
            for (; pos < upTo; ++pos)
            {
                const float s = held >= 0 ? (float) std::sin (phase) * gain->get() : 0.0f;
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
                if (phase > 2.0 * juce::MathConstants<double>::pi) phase -= 2.0 * juce::MathConstants<double>::pi;
                l[pos] = r[pos] = s;
            }
        };
        for (const auto m : midi)
        {
            render (m.samplePosition);
            const auto msg = m.getMessage();
            if (msg.isNoteOn()) { held = msg.getNoteNumber(); freq = msg.getMidiNoteInHertz (held); }
            else if (msg.isNoteOff() && msg.getNoteNumber() == held) held = -1;
            else if (msg.isAllNotesOff()) held = -1;
        }
        render (buffer.getNumSamples());
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::MemoryOutputStream out (dest, false);
        out.writeFloat (gain->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        if (size >= (int) sizeof (float))
        {
            juce::MemoryInputStream in (data, (size_t) size, false);
            *gain = in.readFloat();
        }
    }

private:
    juce::AudioParameterFloat* gain = nullptr;
    double sampleRate = 48000.0, phase = 0.0, freq = 440.0;
    int held = -1;
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkeletonProcessor(); }

const juce::webclap::ClapPluginInfo& juce::webclap::getClapPluginInfo()
{
    static const char* const features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr };
    static const ClapPluginInfo info {
        "is.synth.plugin.skeleton", "Synth.is skeleton", "Synth.is", "https://synth.is", "0.0.1",
        "Skeleton of the Synth.is plugin as a WCLAP: a sine voice, no engines yet", features, "/ui/index.html", 600, 400
    };
    return info;
}
