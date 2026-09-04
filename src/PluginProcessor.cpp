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

// Peak the plugin used to normalise a Faust instrument to.
//
// NO LONGER USED, and kept only so the reason is not lost: the plugin matches
// what the SITE delivers rather than normalising to a target of its own. The
// site is attenuate-only and never boosts, so a quiet elite sounds quiet;
// normalising to 0.25 here disagreed with the audition by however far a genome
// sat from that target, and on 01M1JD666ZTWGTCT4W2GDTVQMF by 31 dB at the very
// pitch the preview renders.
//
// Its last use was inside an implausibility guard, where it silently became the
// level of every hot instrument -- see the note beside that guard.
[[maybe_unused]] static constexpr float kFaustTargetPeak = 0.25f;

// Delivered level for one note at full velocity, matching renderFaustGenome's
// clip protection exactly: attenuate above full scale, never boost.
//
// Headroom for polyphony is deliberately NOT taken here. It would be a silent
// per-instrument level change that puts the plugin back out of step with the
// site, and the honest place for it is one control the user can see.
static constexpr float kFaustCeiling = 1.0f;
#endif

KromosynthAudioProcessor::KromosynthAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // ORDER MATTERS HERE, which it did not before. With
    // JUCE_FORCE_LEGACY_PARAM_IDS the AudioUnit parameter id is the index, so
    // the order these are added is the order a host presents them. Attack,
    // Decay, Sustain, Release first, because that ordering is the whole
    // convention for these four; Output last, because it belongs to the
    // instrument rather than to the envelope.
    //
    // ENVELOPE. These multiply the genome's own rather than replacing it --
    // see FaustEngine::setEnvelope -- so they lengthen an attack but never
    // shorten one, and shorten a tail but never extend it.
    //
    // The defaults are chosen to leave the instrument sounding as it does
    // today, because the site's preview is the reference and an envelope that
    // reshaped every sound on load would break that on contact. A = 0, D = 0,
    // S = 1 is a flat envelope: while a key is held the genome is heard exactly
    // as rendered.
    //
    // R = 1.5 s is the exception, and it matches the grace period the forced
    // release already used, so gate-ignoring genomes stop about when they did
    // before -- but by decaying rather than by being cut off after a fixed wait.
    addParameter (attackS_ = new juce::AudioParameterFloat (
        juce::ParameterID { "attack", 1 }, "Attack",
        juce::NormalisableRange<float> { 0.0f, 5.0f, 0.001f, 0.4f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("s")));

    addParameter (decayS_ = new juce::AudioParameterFloat (
        juce::ParameterID { "decay", 1 }, "Decay",
        juce::NormalisableRange<float> { 0.0f, 5.0f, 0.001f, 0.4f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("s")));

    addParameter (sustain_ = new juce::AudioParameterFloat (
        juce::ParameterID { "sustain", 1 }, "Sustain",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 1.0f));

    addParameter (releaseS_ = new juce::AudioParameterFloat (
        juce::ParameterID { "release", 1 }, "Release",
        juce::NormalisableRange<float> { 0.005f, 10.0f, 0.001f, 0.4f }, 1.5f,
        juce::AudioParameterFloatAttributes().withLabel ("s")));

    // Output level is the visible half of a conflict that cannot be settled
    // silently. Matching the site's preview means one note at full velocity
    // reaches full scale -- that IS the preview's level -- and two notes
    // therefore do not fit. Every fixed compromise is wrong for somebody:
    // 0 dB clips chords, -12 dB quietly contradicts the audition the user just
    // heard. Default 0 dB, which is preview parity, because that is the promise
    // the site makes.
    addParameter (outputLevelDb_ = new juce::AudioParameterFloat (
        juce::ParameterID { "output", 1 },
        "Output",
        juce::NormalisableRange<float> { -36.0f, 6.0f, 0.1f },
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

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

    // A freshly acquired instrument becomes the ACTIVE one.
    //
    // The point of pressing "Add to plugin" from the site is to play the thing.
    // Without this the user has to leave the site, find it in the library and
    // click it -- and finding it was the problem they had in the first place.
    // With it, a MIDI keyboard plays the new instrument while they carry on
    // browsing.
    //
    // It DOES replace whatever was loaded, and that is the intended reading of
    // the request rather than an oversight: the plugin holds one instrument,
    // and the user just asked for this one. Only on success, so a failed
    // download never silences a working instrument.
    //
    // The weak_ptr is the guard that matters: the callback is dispatched from a
    // pool thread through the message queue, so the processor can be gone by
    // the time it runs.
    acquisitions_.setCompletionHandler (
        [this, weak = std::weak_ptr<int> (lifetime_)] (juce::String artifactId, bool ok)
    {
        if (weak.expired() || ! ok || artifactId.isEmpty()) return;

        logEvent ("acquisition_autoload id=" + artifactId);

        {
            const juce::ScopedLock sl (pendingLock_);
            pendingInstrumentId_ = artifactId;
        }

        // loadPendingInstrument needs an engine to load into. When the host has
        // not activated the plugin yet the id simply waits, and prepareToPlay
        // picks it up -- the same route setStateInformation uses.
        bool ready = false;
       #if KROMOSYNTH_HAS_FAUST
        ready = ready || faust_ != nullptr;
       #endif
       #if KROMOSYNTH_HAS_SAMPLER
        ready = ready || engine_ != nullptr;
       #endif

        if (ready)
            loadPendingInstrument();
    });
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

bool KromosynthAudioProcessor::removeInstrument (const juce::String& id)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Stop it BEFORE the files move. The engine holds the module bytes in
    // memory, so a removed instrument would keep playing perfectly well and the
    // user would have deleted something that is still making sound -- and the
    // next reload would then fail on a file that is no longer there.
    if (current_.ok && current_.id == id)
    {
        suspendProcessing (true);

       #if KROMOSYNTH_HAS_FAUST
        if (faust_ != nullptr) faust_->panic();
        faustWasm_ = juce::File{};
        faustJson_ = juce::File{};
        faustDeclared_ = InstrumentLibrary::Metadata{};
        faustSpectrogram_.clear();
       #endif

        active_ = Active::none;
        current_ = LoadOutcome{};

        suspendProcessing (false);
        updateHostDisplay();
    }

    const bool ok = library_.remove (id);

    logEvent ("instrument_removed id=" + id + " ok=" + juce::String (ok ? 1 : 0)
              + " (moved to Trash)");

    return ok;
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

        // A second load while the first is still measuring would call
        // faust_->load() underneath a background render -- new module bytes
        // swapped in while the old ones are being rendered through. The shell
        // disables the list during a load, so this is the backstop for anything
        // that reaches the processor another way.
        if (measuring_.load (std::memory_order_relaxed))
        {
            outcome.detail = "still measuring the previous instrument";
            logEvent ("instrument_load_busy id=" + id);
            return outcome;
        }

        suspendProcessing (true);

        // Whatever was sounding must stop before the module bytes go away.
        // Without this, loading a second instrument to escape a stuck drone
        // would be the natural thing to try and would not work.
        faust_->panic();

        faustWasm_ = resolved.primary;
        faustJson_ = resolved.metadata;

        // What the artefact declares about itself. Read here rather than in
        // applyFaustTrim, which also runs on a sample-rate change and must not
        // touch the disk on that path.
        faustDeclared_ = InstrumentLibrary::readSidecar (resolved.primary.getParentDirectory());

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
        //
        // THE RENDER NOW HAPPENS ON A BACKGROUND THREAD, and the barrier is
        // what makes that safe rather than reckless: processing is suspended,
        // so the audio thread is not in process() and cannot enter it. The
        // background thread therefore has the engine to itself, exactly as the
        // message thread did. What changes is only WHICH thread waits: the
        // message thread returns immediately and the UI stays alive, instead of
        // freezing for the one to three seconds a 105-second measurement takes.
        //
        // suspendProcessing(false) moves with the measurement. Resuming here
        // would open the barrier while the render was still running, which is
        // precisely the race the comment above describes.
        // Read ONCE. This hashes instrument.wasm, and calling it in the
        // condition and again for the value would do that twice.
        auto cached = r.ok ? cachedMeasurement() : std::nullopt;

        if (r.ok && ! runtimeMeasurementNeeded())
        {
            applyFaustTrim();
            suspendProcessing (false);
        }
        else if (r.ok && cached.has_value())
        {
            // MEASURED BEFORE, on this runtime, at this rate, from these exact
            // module bytes. Nothing the measurement depends on has changed, so
            // re-deriving it would spend seconds reproducing numbers already on
            // disk -- which is what switching back and forth between two
            // instruments used to do every time.
            applyFaustMeasurement (*cached, 0.0);
            suspendProcessing (false);

            logEvent ("faust_measure_cached id=" + id
                      + " peak=" + juce::String (cached->peak, 6)
                      + " rtf=" + juce::String (cached->rtf, 1)
                      + " voices=" + juce::String (faustVoices_));
        }
        else if (r.ok)
        {
            faust_->setOutputGain (1.0f);
            measuring_.store (true, std::memory_order_relaxed);

            const auto startedAt = juce::Time::getMillisecondCounterHiRes();

            measurePool_.addJob ([this, id, startedAt,
                                  weak = std::weak_ptr<int> (lifetime_)]
            {
                // The pool is joined before faust_ is destroyed -- see where it
                // is declared -- so the engine is alive for the render itself.
                // The weak_ptr guards the hop back, which goes through the
                // message queue and can outlive the processor.
                auto measured = faust_->measure();
                const auto ms = juce::Time::getMillisecondCounterHiRes() - startedAt;

                juce::MessageManager::callAsync (
                    [this, weak, id, measured = std::move (measured), ms]
                {
                    if (weak.expired()) return;

                    applyFaustMeasurement (measured, ms);
                    suspendProcessing (false);
                    measuring_.store (false, std::memory_order_relaxed);

                    // Written AFTER applying, so a cache is only ever left
                    // behind by a measurement that was actually used.
                    writeCachedMeasurement (measured);

                    LoadOutcome done;
                    done.id = id;
                    done.ok = true;
                    {
                        const juce::ScopedLock sl (outcomeLock_);
                        lastOutcome_ = done;
                    }
                    current_ = done;

                    logEvent ("instrument_measured id=" + id
                              + " ms=" + juce::String (ms, 1));
                    updateHostDisplay();
                });
            });
        }

        if (! r.ok)
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
                  // The mapped range, because an instrument that plays nothing
                  // from a hardware keyboard and everything from an on-screen
                  // one is usually being played outside it.
                  + " keys=" + juce::String (engine_->mappedKeyRange().getStart())
                  + "-" + juce::String (engine_->mappedKeyRange().getEnd())
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

bool KromosynthAudioProcessor::runtimeMeasurementNeeded() const
{
    // ONE definition, because the load path and applyFaustTrim have to agree.
    // If they disagreed, a load could skip the measurement while the apply step
    // expected one, or start a background render whose result is then thrown
    // away in favour of the artefact's numbers.
    //
    // ALSO requires a sweep table. Without one the artefact cannot level the
    // keyboard, and taking its rtf while re-measuring for the table would be
    // two sources of truth for one load. Nothing emits a table yet, so this
    // returns true today -- which is the safe direction.
    return ! (faustDeclared_.hasMeasured
              && faustDeclared_.rtfPlatform.isNotEmpty()
              && faustDeclared_.rtfPlatform == FaustEngine::runtimeId()
              && ! faustDeclared_.pitchPeaks.empty());
}

KromosynthAudioProcessor::LoadOutcome KromosynthAudioProcessor::lastLoadOutcome() const
{
    const juce::ScopedLock sl (outcomeLock_);
    return lastOutcome_;
}

juce::File KromosynthAudioProcessor::cacheFileForCurrentFaust() const
{
    if (faustWasm_ == juce::File{}) return {};
    return faustWasm_.getParentDirectory().getChildFile ("measured.local.json");
}

namespace
{
/** 64-bit FNV-1a over a file's bytes.

    NOT cryptographic, and does not need to be: the only question it answers is
    whether these are the same module bytes that were measured. juce::MD5 and
    juce::SHA256 both live in juce_cryptography, and linking a whole module to
    notice a replaced 28 KB file would be a poor trade.

    Paired with the file SIZE, which is checked separately, so a collision would
    have to be same-length as well.
*/
juce::String hashFile (const juce::File& file)
{
    juce::MemoryBlock block;

    if (! file.loadFileAsData (block))
        return {};

    std::uint64_t h = 1469598103934665603ull;
    const auto* bytes = static_cast<const std::uint8_t*> (block.getData());

    for (size_t i = 0; i < block.getSize(); ++i)
    {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }

    return juce::String::toHexString ((juce::int64) h);
}
} // namespace

std::optional<FaustEngine::Measured> KromosynthAudioProcessor::cachedMeasurement() const
{
    const auto file = cacheFileForCurrentFaust();

    if (file == juce::File{} || ! file.existsAsFile())
        return std::nullopt;

    auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* o = parsed.getDynamicObject();

    if (o == nullptr) return std::nullopt;

    const auto get = [o] (const char* key) { return o->getProperty (key); };

    // Every condition the measurement depends on. A mismatch means discard,
    // never adapt: there is no way to correct a peak measured at another rate
    // or under another runtime, and using it anyway would mis-level the whole
    // keyboard without saying so.
    if ((int) get ("schema") != 1) return std::nullopt;
    if (get ("runtime").toString() != FaustEngine::runtimeId()) return std::nullopt;
    if ((double) get ("sampleRate") != faustRate_) return std::nullopt;
    if ((juce::int64) get ("wasmBytes") != faustWasm_.getSize()) return std::nullopt;

    if (get ("wasmHash").toString() != hashFile (faustWasm_))
        return std::nullopt;

    FaustEngine::Measured m;
    m.peak    = (float) (double) get ("peak");
    m.rtf     = (double) get ("rtf");
    m.capAt4x = (int) get ("capAt4x");
    m.peakC3  = (float) (double) get ("peakC3");
    m.peakA4  = (float) (double) get ("peakA4");
    m.peakA5  = (float) (double) get ("peakA5");

    // WHOLE OR NOTHING for the table. A short one would shift every pitch above
    // the gap, which is the same positional trap the descriptor vector has.
    if (auto* arr = get ("pitchPeaks").getArray())
    {
        if (arr->size() != FaustEngine::kSweepCount) return std::nullopt;

        m.pitchPeaks.reserve ((size_t) arr->size());
        for (const auto& v : *arr) m.pitchPeaks.push_back ((float) (double) v);
    }
    else
    {
        return std::nullopt;
    }

    // The spectrogram is optional: it is a picture, and a missing one costs
    // nothing but a blank panel.
    juce::MemoryOutputStream spectro;
    if (juce::Base64::convertFromBase64 (spectro, get ("spectrogram").toString()))
    {
        const auto* bytes = static_cast<const std::uint8_t*> (spectro.getData());
        m.spectrogram.assign (bytes, bytes + spectro.getDataSize());
    }

    return m;
}

void KromosynthAudioProcessor::writeCachedMeasurement (const FaustEngine::Measured& m) const
{
    const auto file = cacheFileForCurrentFaust();
    if (file == juce::File{}) return;

    auto* o = new juce::DynamicObject();
    o->setProperty ("schema", 1);
    o->setProperty ("runtime", FaustEngine::runtimeId());
    o->setProperty ("sampleRate", faustRate_);
    o->setProperty ("wasmBytes", faustWasm_.getSize());
    o->setProperty ("wasmHash", hashFile (faustWasm_));

    o->setProperty ("peak", m.peak);
    o->setProperty ("rtf", m.rtf);
    o->setProperty ("capAt4x", m.capAt4x);
    o->setProperty ("peakC3", m.peakC3);
    o->setProperty ("peakA4", m.peakA4);
    o->setProperty ("peakA5", m.peakA5);

    juce::Array<juce::var> table;
    for (const auto v : m.pitchPeaks) table.add (v);
    o->setProperty ("pitchPeaks", table);

    if (! m.spectrogram.empty())
        o->setProperty ("spectrogram",
                        juce::Base64::toBase64 (m.spectrogram.data(), m.spectrogram.size()));

    file.replaceWithText (juce::JSON::toString (juce::var (o)));
}

void KromosynthAudioProcessor::applyFaustTrim()
{
    // The caller holds the suspend window.
    if (faust_ == nullptr || ! faust_->isLoaded()) return;

    // ── Can the declared numbers be used instead of measuring? ─────────────
    //
    // ARTEFACT_CONTRACT §3.2 says the plugin should trust the declared trim and
    // re-measure only rtf. That is not implementable as written, and the reason
    // is in FaustEngine::measure: "RTF comes free: it is the wall clock of the
    // render the peak needed." The two numbers are one piece of work. Skipping
    // the peak render to keep the rtf render is not a thing that exists.
    //
    // So the choice is whole or nothing:
    //
    //   runtime matches  -> take trim, cap and rtf from the artefact, render
    //                       nothing. The saving is the entire measurement.
    //
    //   runtime differs  -> measure. The rtf is needed, the peak arrives with
    //                       it, and a peak measured HERE at the host's rate and
    //                       block is strictly better than one measured on a
    //                       server at 48k/512 -- the two differed by 0.66 dB on
    //                       the same module. Preferring the stored value in
    //                       this branch would mean discarding a fresher, more
    //                       accurate number for a staler one.
    //
    // Today rtfPlatform is 'v8-faustwasm' and this is wasmtime, so the second
    // branch is the live one and the declared trim is used as a CROSS-CHECK
    // instead. That is not a consolation prize: it compares a number produced
    // by render-socket against one produced here, after the artefact has been
    // through a socket, a zip, an install and a different wasm runtime. A
    // divergence means something in that chain changed the DSP.
    // ALSO requires a sweep table. Without one the artefact cannot level the
    // keyboard, and taking its rtf while re-measuring for the table would be
    // two sources of truth for one load. Nothing emits a table yet, so this
    // branch does not fire today -- which is the safe direction.
    const bool runtimeMatches = ! runtimeMeasurementNeeded();

    if (runtimeMatches)
    {
        // Same staging as the measured branch below, from the artefact's own
        // numbers: the reference pitch sets the level, the other two set the
        // per-note correction.
        const float ref = faustDeclared_.peakA4 > 0.0f ? faustDeclared_.peakA4
                                                       : faustDeclared_.peak;
        const float trim = ref > 1.0e-6f
            ? juce::jlimit (0.001f, 100.0f, juce::jmin (1.0f, kFaustCeiling / ref))
            : 1.0f;

        faust_->setPitchCompensation (faustDeclared_.pitchPeaks, ref);
        faust_->setOutputCeiling (true);
        faust_->setOutputGain (trim);
        faustTrim_ = trim;
        faustRawPeak_ = faustDeclared_.peak;
        faustVoices_ = juce::jlimit (1, 32, faustDeclared_.capAt4x);
        faust_->setVoiceCount (faustVoices_);
        faustRtf_ = faustDeclared_.rtf;

        // No render happened, so there is nothing to draw. Cleared rather than
        // left stale: showing the PREVIOUS instrument's spectrogram behind this
        // one would be worse than showing none.
        faustSpectrogram_.clear();

        logEvent ("faust_declared rtf=" + juce::String (faustDeclared_.rtf, 1)
                  + " voices=" + juce::String (faustVoices_)
                  + " raw_peak=" + juce::String (faustDeclared_.peak, 6)
                  + " trim=" + juce::String (trim, 4)
                  + " trim_db=" + juce::String (faustDeclared_.trimDb, 1)
                  + " platform=\"" + faustDeclared_.rtfPlatform + "\""
                  + " -- measurement skipped, artefact measured on this runtime");
        return;
    }

    faust_->setOutputGain (1.0f);

    // The cache serves this path too -- it is the one prepareToPlay takes on a
    // sample-rate change, and a rate change is exactly when the old numbers
    // become invalid. cachedMeasurement checks the rate, so a genuine change
    // misses and re-measures while a reopen at the same rate hits.
    if (auto cached = cachedMeasurement())
    {
        logEvent ("faust_measure_cached peak=" + juce::String (cached->peak, 6)
                  + " rtf=" + juce::String (cached->rtf, 1)
                  + " -- sync path");

        applyFaustMeasurement (*cached, 0.0);
        return;
    }

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto measured = faust_->measure();
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    applyFaustMeasurement (measured, ms);
    writeCachedMeasurement (measured);
}

void KromosynthAudioProcessor::applyFaustMeasurement (const FaustEngine::Measured& measured,
                                                      double ms)
{
    // Split out of applyFaustTrim so the RENDER can happen on a background
    // thread while this half stays on the message thread. Everything here
    // touches engine settings and editor-visible state; nothing here renders.
    if (faust_ == nullptr) return;

    const float peak = measured.peak;

    // ── level, matched to the preview ───────────────────────────────────────
    //
    // The REFERENCE PITCH sets the level, and the other two pitches only set
    // the per-note correction. a4 is the reference because a preview renders at
    // the sound's stored noteDelta, which is 0 for the sounds checked, and 0 is
    // a4 by the same convention freqForNoteDelta uses on the render server.
    //
    // Attenuate-only, exactly as renderFaustGenome does: a genome quieter than
    // full scale is left alone, so a quiet elite is quiet here too. Boosting it
    // to a target is what made the plugin disagree with the audition.
    const float reference = measured.peakA4 > 0.0f ? measured.peakA4 : peak;

    // A genome that measured silent gets no trim rather than an infinite one.
    float trim = reference > 1.0e-6f ? juce::jmin (1.0f, kFaustCeiling / reference) : 1.0f;

    // Worst case the sweep actually saw, after correction. Reported rather than
    // corrected for: it is the honest answer to "can this instrument clip", and
    // the ceiling in the engine is what stops it doing so audibly.
    float worstCorrected = 0.0f;
    for (size_t i = 0; i < measured.pitchPeaks.size(); ++i)
    {
        const float p = measured.pitchPeaks[i];
        if (p <= 1.0e-6f) continue;
        const float comp = juce::jmin (1.0f, reference / p);
        worstCorrected = juce::jmax (worstCorrected, p * comp);
    }

    // ONLY a broken measurement, not merely a loud one.
    //
    // This tested `peak > 8.0` and replaced the trim with a fixed 0.03125,
    // written when ARTEFACT_NUMBERS had measured nothing in the archive above
    // 2.4. The TIMBRE-seeded runs peak at 56, 706 and 1990 legitimately, so the
    // guard fired on CORRECT measurements and discarded the computed trim --
    // 0.03125 against a correct 0.00142 is 26.9 dB too loud, which is an
    // instrument delivering 22.0 into a ceiling built for 1.0. It also made
    // three instruments with references of 706, 8.1 and 0.54 all log the same
    // trim, which is what gave it away.
    //
    // The real failure it was written for is a peak of 23182060, seen once when
    // the measurement raced the audio thread. That is not a loud genome, it is
    // a broken number, and the threshold now says so. Nothing else needs a
    // special case: trim = min(1, 1/reference) is bounded and correct for every
    // finite peak.
    const bool implausible = ! std::isfinite (peak) || peak > 1.0e4f;
    if (implausible)
        trim = 1.0e-4f;

    trim = juce::jlimit (1.0e-6f, 1.0f, trim);

    // Every note levelled to the reference pitch, so the whole keyboard plays
    // at the level the audition established rather than only one key of it.
    faust_->setPitchCompensation (measured.pitchPeaks, reference);

    // Enabled only now, and only for playback. measure() has just finished, and
    // running it with the ceiling on would have reported a peak the ceiling
    // invented rather than the genome's.
    faust_->setOutputCeiling (true);

    faust_->setOutputGain (trim);
    faustTrim_ = trim;
    faustRawPeak_ = peak;
    faustSpectrogram_ = measured.spectrogram;

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
              + " c3=" + juce::String (measured.peakC3, 4)
              + " a4=" + juce::String (measured.peakA4, 4)
              + " a5=" + juce::String (measured.peakA5, 4)
              + " spread_db=" + juce::String (20.0f * std::log10 (
                    juce::jmax (1.0e-9f, peak)
                    / juce::jmax (1.0e-9f, juce::jmin (measured.peakC3,
                                  juce::jmin (measured.peakA4, measured.peakA5)))), 1)
              + " reference=" + juce::String (reference, 6)
              + " sweep_worst=" + juce::String (worstCorrected, 4)
              + " trim=" + juce::String (trim, 4)
              + " trim_db=" + juce::String (20.0f * std::log10 (juce::jmax (1.0e-9f, trim)), 1)
              + " measure_ms=" + juce::String (ms, 1));

    // ── the cross-check ─────────────────────────────────────────────────
    //
    // The peak the server declared against the peak measured here, on the same
    // module after a socket, a zip and an install. Two independent
    // implementations over two wasm runtimes, so agreement is evidence the
    // whole chain preserved the DSP -- and this is the only place that
    // comparison happens on a user's machine rather than in a test.
    //
    // A 2 dB band, not equality. The rate and block differ from the server's
    // 48k/512 and that alone moved a peak by 0.66 dB, so a tight bound would
    // report the difference between two correct measurements. 2 dB is wide
    // enough to ignore that and narrow enough that a wrong module, a truncated
    // download or a compile divergence does not hide in it.
    if (faustDeclared_.hasMeasured && faustDeclared_.peak > 1.0e-6f && peak > 1.0e-6f)
    {
        const float deltaDb = 20.0f * std::log10 (peak / faustDeclared_.peak);

        if (std::abs (deltaDb) > 2.0f)
            logEvent ("faust_peak_divergence declared="
                      + juce::String (faustDeclared_.peak, 6)
                      + " measured=" + juce::String (peak, 6)
                      + " delta_db=" + juce::String (deltaDb, 2)
                      + " declared_at=" + juce::String (faustDeclared_.rtfPlatform)
                      + " -- the artefact and this engine disagree about the same module");
    }
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

    // Read once per block. Faust takes it as an engine setting so that it lands
    // BEFORE the limiter -- turning the level down has to move the signal out
    // of limiting, not just make the limited result quieter. The sampler has no
    // such stage, so it is applied to the buffer afterwards.
    const float levelDb = outputLevelDb_ != nullptr ? outputLevelDb_->get() : 0.0f;
    const float level = juce::Decibels::decibelsToGain (levelDb, -36.0f);

    // Pushed per block rather than per note, so a moving automation lane is
    // heard on notes already sounding rather than only on the next one.
    //
    // BOTH engines, by different routes: Faust applies its own envelope per
    // voice, the sampler shapes sfizz's through the CC modulation the SFZ
    // already declares. Same controls, same reading -- 0 leaves the artefact
    // as generated -- but not the same mechanism, so neither is pretended to
    // be the other.
   #if KROMOSYNTH_HAS_FAUST
    if (faust_ != nullptr && attackS_ != nullptr)
        faust_->setEnvelope (attackS_->get(), decayS_->get(),
                             sustain_->get(), releaseS_->get());
   #endif

   #if KROMOSYNTH_HAS_SAMPLER
    if (engine_ != nullptr && attackS_ != nullptr)
        engine_->setEnvelope (attackS_->get(), decayS_->get(),
                              sustain_->get(), releaseS_->get());
   #endif

    // Routed on active_ rather than on "whichever engine exists", because both
    // engines can be built and both can be constructed while only one holds an
    // instrument. Falling through to the sampler because it happens to be
    // non-null would play silence over a loaded Faust instrument, and silence
    // is the failure mode this codebase keeps having to prove it does not have.
   #if KROMOSYNTH_HAS_SAMPLER
    if (active_ == Active::sampler && engine_ != nullptr)
    {
        engine_->process (buffer, midiMessages);
        buffer.applyGain (level);
        return;
    }
   #endif

   #if KROMOSYNTH_HAS_FAUST
    if (active_ == Active::faust && faust_ != nullptr)
    {
        faust_->setOutputLevel (level);
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
