// DISPOSABLE — see PluginProcessor.h.

#include "PluginProcessor.h"
#include "PluginEditor.h"

KromosynthAudioProcessor::KromosynthAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // One log file per host process, named after the host, because the whole
    // Stage 0 question is "does this behave differently in Logic vs Live vs
    // Reaper vs Bitwig" and mixing four hosts into one file loses that.
    const auto hostName = juce::File::getSpecialLocation (
                              juce::File::hostApplicationPath).getFileNameWithoutExtension();

    logFile_ = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Logs/Synth.is")
                   .getChildFile ("stage0-" + hostName + ".log");

    logFile_.getParentDirectory().createDirectory();
    logEvent ("processor_constructed host=" + hostName
              + " wrapper=" + juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
}

void KromosynthAudioProcessor::logEvent (const juce::String& event)
{
    // Timestamped, append-only, flushed per line. Slow and completely
    // acceptable: nothing here runs on the audio thread.
    const auto line = juce::Time::getCurrentTime().toISO8601 (true) + "  " + event + "\n";
    logFile_.appendText (line, false, false, "\n");
    DBG ("[stage0] " << event);
}

void KromosynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    logEvent ("prepareToPlay sampleRate=" + juce::String (sampleRate)
              + " blockSize=" + juce::String (samplesPerBlock));
}

void KromosynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // No engine. Silence, deliberately.
    //
    // Worth stating because it is a Stage 0 finding in waiting: the web app in
    // the webview plays audio through the page's own Web Audio context
    // (@elemaudio/web-renderer, virtual-audio-graph), which goes to the SYSTEM
    // default output — not through this buffer. So auditioning in the plugin
    // will be audible while this method returns silence, and the DAW will
    // neither route nor record it. That is a design problem for WP §5, not a
    // bug to fix here.
    buffer.clear();
}

juce::AudioProcessorEditor* KromosynthAudioProcessor::createEditor()
{
    return new KromosynthAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KromosynthAudioProcessor();
}
