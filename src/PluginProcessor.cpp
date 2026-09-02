// DISPOSABLE — see PluginProcessor.h.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#if KROMOSYNTH_HAS_SAMPLER
 #include "SamplerEngine.h"
#endif

#if KROMOSYNTH_HAS_FAUST
 #include "FaustEngine.h"

// Fixed for the skeleton. §2.7's per-genome cap is the real answer -- measured
// single-voice RTF carried alongside the artefact, from which a per-platform
// cap follows arithmetically -- and nothing carries that number yet. 8 is the
// polyphony §3.3 asks about, so it is at least the figure the archive was
// characterised at rather than one picked here.
static constexpr int kFaustVoices = 8;

// Peak the plugin trims a Faust instrument to, matching trimForLevel in
// kromosynth-composing so a genome trimmed there and here lands in the same
// place.
//
// Measured over 120 archive genomes, raw peaks span 0.0236 to 2.3975 -- a 40 dB
// range that nothing normalises. The loudest clips by 7.6 dB on ONE voice at
// full velocity, before polyphony. Without a trim, choosing an instrument is a
// coin flip between inaudible and painful, and the user cannot tell which is
// coming.
//
// MEASURED AT LOAD, not carried with the artefact, because the artefact carries
// nothing yet. ARTEFACT_NUMBERS_2026-09-01 argues it should; when it does, the
// stored value is preferred and this measurement is skipped.
static constexpr float kFaustTargetPeak = 0.25f;
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

KromosynthAudioProcessor::EngineState KromosynthAudioProcessor::engineState() const
{
    EngineState st;
   #if KROMOSYNTH_HAS_FAUST
    if (faust_ != nullptr && faust_->isLoaded())
    {
        st.faust = true;
        st.notesOn = faust_->notesOn();
        st.notesOff = faust_->notesOff();
        st.activeVoices = faust_->activeVoices();
        st.forcedStops = faust_->forcedStops();
        st.trimDb = 20.0f * std::log10 (juce::jmax (1.0e-9f, faustTrim_));
        st.rawPeak = faustRawPeak_;
        st.rtf = (float) faustRtf_;
        st.voiceCap = faustVoices_;
    }
   #endif
    return st;
}

// ── session state ───────────────────────────────────────────────────────────
//
// The tag and version are written so that a later reader can tell "this is not
// ours" from "this is an older ours". Without a version, the first schema
// change either breaks old sessions or has to guess.
static const juce::Identifier kStateTag ("SynthIsPluginState");
static const juce::Identifier kStateVersion ("version");
static const juce::Identifier kStateInstrument ("instrumentId");

void KromosynthAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree tree (kStateTag);
    tree.setProperty (kStateVersion, 1, nullptr);

    // current_.id is set on a successful load only, so a failed load does not
    // get saved and then fail again on every reopen.
    tree.setProperty (kStateInstrument, current_.ok ? current_.id : juce::String(), nullptr);

    juce::MemoryOutputStream out (dest, false);
    tree.writeToStream (out);

    logEvent ("state_saved instrument=\"" + current_.id + "\"");
}

void KromosynthAudioProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;

    const auto tree = juce::ValueTree::readFromData (data, (size_t) size);

    // Not ours, or corrupt. Silently ignoring is right -- a host handing us
    // another plugin's block is a host bug, not a reason to lose the session.
    if (! tree.isValid() || ! tree.hasType (kStateTag))
    {
        logEvent ("state_ignored reason=unrecognised size=" + juce::String (size));
        return;
    }

    const auto id = tree.getProperty (kStateInstrument).toString();

    {
        const juce::ScopedLock sl (pendingLock_);
        pendingInstrumentId_ = id;
    }

    logEvent ("state_restored instrument=\"" + id + "\" version="
              + tree.getProperty (kStateVersion).toString());

    if (id.isEmpty()) return;

    // A host may call this before OR after prepareToPlay. If the engines are
    // already up, load now -- but on the message thread, because loadInstrument
    // suspends processing and calls updateHostDisplay, and setStateInformation
    // carries no thread guarantee.
    bool ready = false;
   #if KROMOSYNTH_HAS_FAUST
    ready = ready || faust_ != nullptr;
   #endif
   #if KROMOSYNTH_HAS_SAMPLER
    ready = ready || engine_ != nullptr;
   #endif

    if (! ready) return;   // prepareToPlay will pick it up

    juce::MessageManager::callAsync ([weak = std::weak_ptr<int> (lifetime_), this]
    {
        if (weak.expired()) return;   // the plugin went away before we ran
        loadPendingInstrument();
    });
}

void KromosynthAudioProcessor::loadPendingInstrument()
{
    juce::String id;
    {
        const juce::ScopedLock sl (pendingLock_);
        id = pendingInstrumentId_;
        pendingInstrumentId_.clear();
    }

    if (id.isEmpty()) return;

    const auto outcome = loadInstrument (id);

    // A saved instrument can be gone -- deleted, or the project opened on
    // another machine. Logged rather than silently dropped, because "my
    // instrument did not come back" needs an answer.
    if (! outcome.ok)
        logEvent ("state_load_failed instrument=\"" + id + "\" detail=\"" + outcome.detail + "\"");
}

void KromosynthAudioProcessor::panic()
{
    JUCE_ASSERT_MESSAGE_THREAD

    suspendProcessing (true);

   #if KROMOSYNTH_HAS_FAUST
    if (faust_ != nullptr) faust_->panic();
   #endif

    suspendProcessing (false);
    logEvent ("panic");
}

KromosynthAudioProcessor::LoadOutcome
KromosynthAudioProcessor::loadInstrument (const juce::String& id)
{
    JUCE_ASSERT_MESSAGE_THREAD

    LoadOutcome outcome;
    outcome.id = id;

    // The folder decides which engine plays it -- an instrument.sfz is a
    // rendered sample set, an instrument.wasm plus its .json is a live Faust
    // program. The shell calls one function either way and does not have to
    // know, because the artefact already does.
    const auto resolved = library_.resolve (id);

    if (resolved.kind == InstrumentLibrary::Kind::none)
    {
        // resolve rejects ids containing separators or ".." as well as ones
        // that simply are not there, and deliberately does not distinguish:
        // telling a caller which of the two happened is a directory oracle.
        outcome.detail = "no such instrument in the local library";
        logEvent ("instrument_load_rejected id=" + id);
        return outcome;
    }

   #if KROMOSYNTH_HAS_FAUST
    if (resolved.kind == InstrumentLibrary::Kind::faust)
    {
        if (faust_ == nullptr)
        {
            outcome.detail = "plugin not activated by the host yet";
            logEvent ("instrument_load_deferred id=" + id);
            return outcome;
        }

        suspendProcessing (true);

        // Whatever was sounding must stop before the module bytes go away.
        // Without this, loading a second instrument to escape a stuck drone
        // would be the natural thing to try and would not work.
        faust_->panic();

        faustWasm_ = resolved.primary;
        faustJson_ = resolved.metadata;

        // ONE voice to start. Measuring needs an instance, and the polyphony
        // that measurement implies is not known until it has run.
        const auto r = faust_->load (faustWasm_, faustJson_, faustRate_,
                                     faustBlock_, 1);

        if (r.ok)
            active_ = Active::faust;

        // INSIDE the suspend window. applyFaustTrim renders through
        // FaustEngine::process, which is the same call the audio thread makes,
        // on the same voices and the same shared mix buffer. Measuring after
        // releasing the barrier raced the audio thread and produced a raw peak
        // of 23182060 -- an audible bang, and a -159 dB trim that silenced
        // every instrument loaded afterwards.
        if (r.ok)
            applyFaustTrim();

        suspendProcessing (false);

        outcome.ok = r.ok;
        outcome.detail = r.detail;

        logEvent ("instrument_load id=" + id + " kind=faust"
                  + " ok=" + juce::String (r.ok ? 1 : 0)
                  + " dsp=\"" + r.dspName + "\""
                  // faustVoices_, not r.voices. The load deliberately
                  // instantiates ONE voice so the genome can be measured, and
                  // applyFaustTrim resizes to the measured cap afterwards.
                  // Reporting r.voices printed "voices=1" in the same
                  // millisecond as faust_measure printing "voices=32", which
                  // leaves a reader of the log unable to tell which is true.
                  + " voices=" + juce::String (faustVoices_)
                  + " gate=" + juce::String (r.hasGate ? 1 : 0)
                  + (r.detail.isEmpty() ? juce::String()
                                        : " detail=\"" + r.detail + "\""));

        if (r.ok)
            current_ = outcome;

        updateHostDisplay();
        return outcome;
    }
   #endif

    const auto file = resolved.primary;

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
    {
        current_ = outcome;
        active_ = Active::sampler;
    }

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

        // Without this the compiled-in default SFZ loads and then plays
        // nothing, because processBlock routes on active_ rather than on
        // whichever engine happens to be non-null.
        if (result.ok)
            active_ = Active::sampler;

        // §G4: five voices per note on an XY instrument, at every position,
        // because sfizz does not cull voices whose amplitude modulation has
        // driven them to zero. At sfizz's default 64 (Config.h:37) that is
        // about twelve simultaneous notes. Logged rather than remembered, and
        // deliberately NOT changed with setNumVoices — voice stealing is
        // audible and that decision should follow a measurement.
    }

    suspendProcessing (false);
   #endif

   #if KROMOSYNTH_HAS_FAUST
    suspendProcessing (true);

    if (faust_ == nullptr)
        faust_ = std::make_unique<FaustEngine>();

    // Recorded here rather than inside reloadFaustIfNeeded, which returns
    // early when no instrument is set. loadInstrument needs a rate and a block
    // size to load AT, and the only place they arrive is here.
    faustRate_ = sampleRate;
    faustBlock_ = samplesPerBlock;

    reloadFaustIfNeeded (sampleRate, samplesPerBlock);

    suspendProcessing (false);
   #endif

    // If setStateInformation ran first, this is where the instrument it named
    // actually gets loaded -- the engines only exist now.
    loadPendingInstrument();
}

#if KROMOSYNTH_HAS_FAUST

void KromosynthAudioProcessor::reloadFaustIfNeeded (double sampleRate, int samplesPerBlock)
{
    // The caller holds the suspend window.
    if (faust_ == nullptr || faustWasm_ == juce::File{})
        return;

    // Unlike sfizz there is nothing to retune. init() writes the sample rate
    // into the DSP struct and the block size fixes the wasm memory layout, so
    // a change in either means loading again from the module bytes. A LARGER
    // block is the case that would corrupt rather than merely detune: the
    // output buffer inside the module is sized for what was asked at load.
    if (faust_->isLoaded()
        && juce::approximatelyEqual (faustRate_, sampleRate)
        && faustBlock_ >= samplesPerBlock)
        return;

    const auto result = faust_->load (faustWasm_, faustJson_, sampleRate,
                                      samplesPerBlock, 1);

    logEvent ("faust_load ok=" + juce::String (result.ok ? 1 : 0)
              + " dsp=\"" + result.dspName + "\""
              + " struct=" + juce::String (result.structBytes)
              + " outputs=" + juce::String (result.outputs)
              + " voices=" + juce::String (result.voices)
              + " rate=" + juce::String (sampleRate)
              + " block=" + juce::String (samplesPerBlock)
              + " gate=" + juce::String (result.hasGate ? 1 : 0)
              + " freq=" + juce::String (result.hasFreq ? 1 : 0)
              + " gain=" + juce::String (result.hasGain ? 1 : 0)
              + (result.detail.isEmpty() ? juce::String()
                                         : " detail=\"" + result.detail + "\""));

    if (! result.ok)
    {
        active_ = Active::none;
        return;
    }

    applyFaustTrim();
}

void KromosynthAudioProcessor::applyFaustTrim()
{
    // The caller holds the suspend window.
    if (faust_ == nullptr || ! faust_->isLoaded()) return;

    faust_->setOutputGain (1.0f);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto measured = faust_->measure();
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    const float peak = measured.peak;

    // A genome that measured silent gets no trim rather than an infinite one.
    float trim = peak > 1.0e-6f ? kFaustTargetPeak / peak : 1.0f;

    // A measurement can be wrong, and a wrong one used verbatim is worse than
    // none: a raw peak of 23182060 yields -159 dB and silences the instrument
    // with no indication why. Nothing in the archive exceeds 2.4 -- measured
    // across all 120 -- so a peak past 8 is a broken measurement rather than a
    // loud genome. Clamped and logged rather than trusted.
    const bool implausible = peak > 8.0f;
    if (implausible)
        trim = kFaustTargetPeak / 8.0f;

    trim = juce::jlimit (0.001f, 100.0f, trim);

    faust_->setOutputGain (trim);
    faustTrim_ = trim;
    faustRawPeak_ = peak;

    // Polyphony sized to THIS genome rather than to a constant. The archive
    // spans 36x to 2231x single-voice RTF, so a fixed 8 either wastes an
    // instrument that could carry 32 voices or overcommits one that cannot.
    // §2.7's per-genome cap, computed here because the artefact carries nothing
    // yet; when it does, the stored value is preferred and this is skipped.
    faustVoices_ = juce::jlimit (1, 32, measured.capAt4x);
    faust_->setVoiceCount (faustVoices_);
    faustRtf_ = measured.rtf;

    if (implausible)
        logEvent ("faust_trim_implausible raw_peak=" + juce::String (peak, 3)
                  + " clamped=" + juce::String (trim, 6)
                  + " -- nothing in the archive exceeds 2.4; suspect the measurement");

    logEvent ("faust_measure rtf=" + juce::String (measured.rtf, 1)
              + " voices=" + juce::String (faustVoices_)
              + " raw_peak=" + juce::String (peak, 6)
              + " target=" + juce::String (kFaustTargetPeak, 3)
              + " trim=" + juce::String (trim, 4)
              + " trim_db=" + juce::String (20.0f * std::log10 (juce::jmax (1.0e-9f, trim)), 1)
              + " measure_ms=" + juce::String (ms, 1));
}

#endif

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

    // Routed on active_ rather than on "whichever engine exists", because both
    // engines can be built and both can be constructed while only one holds an
    // instrument. Falling through to the sampler because it happens to be
    // non-null would play silence over a loaded Faust instrument, and silence
    // is the failure mode this codebase keeps having to prove it does not have.
   #if KROMOSYNTH_HAS_SAMPLER
    if (active_ == Active::sampler && engine_ != nullptr)
    {
        engine_->process (buffer, midiMessages);
        return;
    }
   #endif

   #if KROMOSYNTH_HAS_FAUST
    if (active_ == Active::faust && faust_ != nullptr)
    {
        faust_->process (buffer, midiMessages);
        return;
    }
   #endif

   #if ! (KROMOSYNTH_HAS_SAMPLER || KROMOSYNTH_HAS_FAUST)
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

bool KromosynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no input bus, one output bus, mono or stereo.
    //
    // Declared rather than left to JUCE's permissive default, which advertised
    // 4, 5, 6, 7 and 8 channel outputs the engine cannot fill. See the header
    // for the auval warning that surfaced it. Mono is kept because
    // SamplerEngine::process genuinely implements it (the scratch downmix),
    // not because a host might ask.
    if (layouts.getMainInputChannels() != 0)
        return false;

    const auto out = layouts.getMainOutputChannelSet();

    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* KromosynthAudioProcessor::createEditor()
{
    return new KromosynthAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KromosynthAudioProcessor();
}
