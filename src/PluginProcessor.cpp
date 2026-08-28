// DISPOSABLE — see PluginProcessor.h.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#if KROMOSYNTH_HAS_SAMPLER
 #include "SamplerEngine.h"
#endif

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

KromosynthAudioProcessor::~KromosynthAudioProcessor()
{
   #if KROMOSYNTH_HAS_SAMPLER
    logRenderStats ("destructor");
   #endif
}

#if KROMOSYNTH_HAS_SAMPLER

juce::File KromosynthAudioProcessor::sfzFile() const
{
    const juce::String path (KROMOSYNTH_SFZ_PATH);

    if (path.isEmpty())
        return {};

    return juce::File (path);
}

void KromosynthAudioProcessor::logRenderStats (const char* reason)
{
    // NOT once-only, and the first version of this was.
    //
    // The once-only guard existed to avoid a duplicate line in hosts that call
    // releaseResources before destroying. It instead LATCHED THE MEASUREMENT
    // AT ZERO: auval calls releaseResources during its property and
    // configuration passes, long before it renders anything, so the single
    // line it emitted read blocks=0 peak=0 for a plugin that went on to render
    // normally. A diagnostic that reports the failure it was built to detect,
    // for a healthy build, is worse than no diagnostic.
    //
    // The counters are cumulative for the lifetime of a load, so logging on
    // every call gives a monotonic series whose last line is the total. Noisier
    // and correct beats tidy and wrong.
    if (engine_ == nullptr)
        return;

    const auto s = engine_->stats();

    // The peak is the whole point. "blocks=4210 peak=0" is a loaded instrument
    // that rendered nothing audible, which is the F11 failure mode surviving
    // all the way to the audio thread; "blocks=0" is a plugin the host never
    // asked to render. Without this they are the same silence.
    logEvent (juce::String ("sampler_render_stats reason=") + reason
              + " blocks=" + juce::String (s.blocks)
              + " frames=" + juce::String (s.frames)
              + " peak=" + juce::String (s.peak, 6)
              + " max_host_block=" + juce::String (s.maxHostBlock)
              + " max_chunks=" + juce::String (s.maxChunks));
}

#endif

double KromosynthAudioProcessor::getTailLengthSeconds() const
{
   #if KROMOSYNTH_HAS_SAMPLER
    return engine_ != nullptr ? engine_->tailSeconds() : 0.0;
   #else
    return 0.0;
   #endif
}

KromosynthAudioProcessor::LoadOutcome
KromosynthAudioProcessor::loadInstrument (const juce::String& id)
{
    JUCE_ASSERT_MESSAGE_THREAD

    LoadOutcome outcome;
    outcome.id = id;

    const auto file = library_.pathFor (id);

    if (file == juce::File{})
    {
        // pathFor rejects ids containing separators or ".." as well as ones
        // that simply are not there, and deliberately does not distinguish:
        // telling a caller which of the two happened is a directory oracle.
        outcome.detail = "no such instrument in the local library";
        logEvent ("instrument_load_rejected id=" + id);
        return outcome;
    }

    if (engine_ == nullptr)
    {
        // prepareToPlay has not run, so there is nothing to load into and no
        // sample rate to load at. Recording the request would mean a pending
        // state the rest of this file does not have; the shell can retry once
        // the host activates the plugin.
        outcome.detail = "plugin not activated by the host yet";
        logEvent ("instrument_load_deferred id=" + id);
        return outcome;
    }

    // Same barrier as prepareToPlay, for the same reason: loadSfzFile is OFF
    // (sfizz.hpp:118-120) and must not run while a render is in flight. That
    // the caller here is JavaScript on the message thread rather than the host
    // changes nothing about sfizz's requirement.
    suspendProcessing (true);

    const auto result = engine_->load (file);

    suspendProcessing (false);

    outcome.ok = result.ok;
    outcome.numRegions = result.numRegions;
    outcome.tailSeconds = result.tailSeconds;
    outcome.detail = result.detail;

    logEvent ("instrument_load id=" + id
              + " ok=" + juce::String (result.ok ? 1 : 0)
              + " regions=" + juce::String (result.numRegions)
              + " ms=" + juce::String (result.loadMs, 1)
              + " tail_s=" + juce::String (result.tailSeconds, 3)
              + (result.detail.isEmpty() ? juce::String()
                                         : " detail=\"" + result.detail + "\""));

    if (result.ok)
        current_ = outcome;

    // The host's reported latency and tail are now stale. Tail in particular:
    // a host that cached 8.3 s from the previous instrument will truncate or
    // over-render this one.
    updateHostDisplay();

    return outcome;
}

void KromosynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    logEvent ("prepareToPlay sampleRate=" + juce::String (sampleRate)
              + " blockSize=" + juce::String (samplesPerBlock));

   #if KROMOSYNTH_HAS_SAMPLER
    // ── The OFF-thread barrier ───────────────────────────────────────────
    //
    // loadSfzFile, setSampleRate and setSamplesPerBlock are all documented OFF
    // (sfizz.hpp:118-120, :278-280, :293-295): they must not run while another
    // thread is inside an RT function. JUCE's contract says prepareToPlay is
    // not concurrent with processBlock, but a contract is not a barrier and
    // hosts do change sample rate mid-session.
    //
    // suspendProcessing IS the barrier, read out of JUCE 9.0.1 rather than
    // assumed:
    //
    //   • Both wrappers hold AudioProcessor::getCallbackLock() across the
    //     WHOLE processBlock call and test isSuspended() INSIDE that lock
    //     (juce_audio_plugin_client_AU_1.mm:2189-2203,
    //      juce_audio_plugin_client_VST3.cpp:3714-3735).
    //   • suspendProcessing takes the same lock
    //     (juce_AudioProcessor.cpp:583-587).
    //
    // So it cannot return while a block is in flight, and no later block can
    // enter our code until we clear the flag. It also supplies the
    // happens-before edge for engine_ itself.
    //
    // One AU path calls prepareToPlay while ALREADY holding that lock
    // (AU_1.mm:804-809, the offline-render property setter). juce::CriticalSection
    // is a recursive pthread mutex (juce_SharedCode_posix.h:42), so this
    // re-acquires rather than deadlocking — and on that path the audio thread
    // is already excluded by the same lock, so it does not need to be a
    // barrier.
    //
    // HELD AS A SOURCE READING, not a measurement, and scoped to these two
    // wrappers at this JUCE pin.

    suspendProcessing (true);

    if (engine_ == nullptr)
        engine_ = std::make_unique<SamplerEngine>();

    engine_->prepare (sampleRate, samplesPerBlock);

    // Load once. A later prepareToPlay at a different rate re-prepares only:
    // Synth::setSampleRate (Synth.cpp:1073) retunes voices and resources in
    // place and needs no reload.
    if (! engine_->isLoaded())
    {
        const auto file = sfzFile();
        const auto result = engine_->load (file);

        logEvent ("sampler_load ok=" + juce::String (result.ok ? 1 : 0)
                  + " regions=" + juce::String (result.numRegions)
                  + " voices=" + juce::String (result.numVoices)
                  + " ms=" + juce::String (result.loadMs, 1)
                  + " tail_s=" + juce::String (result.tailSeconds, 3)
                  + " tail_ms=" + juce::String (result.tailMs, 1)
                  + (result.detail.isEmpty() ? juce::String()
                                             : " detail=\"" + result.detail + "\"")
                  + " path=" + file.getFullPathName());

        // §G4: five voices per note on an XY instrument, at every position,
        // because sfizz does not cull voices whose amplitude modulation has
        // driven them to zero. At sfizz's default 64 (Config.h:37) that is
        // about twelve simultaneous notes. Logged rather than remembered, and
        // deliberately NOT changed with setNumVoices — voice stealing is
        // audible and that decision should follow a measurement.
    }

    suspendProcessing (false);
   #endif
}

void KromosynthAudioProcessor::releaseResources()
{
   #if KROMOSYNTH_HAS_SAMPLER
    logRenderStats ("releaseResources");
   #endif
}

void KromosynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

   #if KROMOSYNTH_HAS_SAMPLER
    if (engine_ != nullptr)
    {
        engine_->process (buffer, midiMessages);
        return;
    }
   #else
    juce::ignoreUnused (midiMessages);
   #endif

    // No engine yet, or built without the sampler. Silence.
    //
    // Worth restating, because it did not stop being true when the sampler
    // arrived: the web app in the webview plays audio through the page's own
    // Web Audio context (@elemaudio/web-renderer, virtual-audio-graph), which
    // goes to the SYSTEM default output — not through this buffer. Audition
    // and playback are two different audio paths at two possibly different
    // sample rates, and §C decided that stays true: audition lives in the
    // webview, the plugin plays instruments. A user who auditions and then
    // loads will be comparing them.
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
