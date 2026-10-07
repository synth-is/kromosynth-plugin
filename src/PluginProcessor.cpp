// DISPOSABLE — see PluginProcessor.h.

#include "PluginProcessor.h"
#include "PluginEditor.h"
// For drain(), which the CONTAINER APP runs at launch to move renders the appex
// left in the App Group into a Documents folder the appex cannot reach.
#include "RenderOutbox.h"

#ifndef KROMOSYNTH_MEASURE_EPOCH
 #define KROMOSYNTH_MEASURE_EPOCH 1
#endif

#if JUCE_MAC || JUCE_IOS || JUCE_LINUX
 // For the atomic log append below. juce::File cannot express O_APPEND, which
 // is the whole point -- see appendLineAtomically.
 #include <fcntl.h>
 #include <unistd.h>
 #include <cstring>
#endif

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

namespace
{
/** A float parameter whose DISPLAYED NAME can change after construction.

    EXPERIMENTAL, and narrow on purpose. It exists to answer one question before
    any real work is built on the answer: does a host update a parameter's name
    when the plugin changes it?

    The question matters because AU and VST3 parameter lists are fixed at
    construction -- a host reads them once -- while each Faust genome exposes a
    DIFFERENT set of sliders: 31 distinct names across 22 seeds, 6 to 10 per
    seed. Per-instrument named parameters are therefore impossible, and the only
    route is a fixed pool renamed on load. If names do not refresh, that pool is
    eight knobs labelled "Macro 1..8" with no indication of what they do, which
    is worse than four that work -- and MIDI CC mapping becomes the better
    answer for hardware control.

    GarageBand is the specific doubt: it cached parameter ORDER until the host
    process was fully quit, so it is a poor bet for refreshing parameter NAMES.

    This one does NOTHING to the audio. It renames itself to the first slider
    the loaded genome exposes and stops there. Deliberately: the identical
    change that also wrote to the genome was reverted an hour ago for measuring
    the instrument after altering it, and the ordering that fixes that is not
    what is being tested here.
*/
class RenamableParameter final : public juce::AudioParameterFloat
{
public:
    using juce::AudioParameterFloat::AudioParameterFloat;

    juce::String getName (int maximumLength) const override
    {
        const juce::ScopedLock sl (nameLock_);
        return displayName_.isEmpty() ? juce::AudioParameterFloat::getName (maximumLength)
                                      : displayName_.substring (0, maximumLength);
    }

    void setDisplayName (const juce::String& name, juce::AudioProcessor& owner)
    {
        {
            const juce::ScopedLock sl (nameLock_);
            if (displayName_ == name) return;
            displayName_ = name;
        }

        // withParameterInfoChanged is what asks the host to re-read names and
        // ranges. Whether it acts on it is the experiment. The processor is
        // passed in because AudioProcessorParameter does not expose its owner.
        owner.updateHostDisplay (juce::AudioProcessorListener::ChangeDetails()
                                     .withParameterInfoChanged (true));
    }

private:
    mutable juce::CriticalSection nameLock_;
    juce::String displayName_;
};
} // namespace

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

    // Output level, and the default is -9 dB because that is what the audition
    // actually delivers.
    //
    // THIS COMMENT USED TO SAY 0 dB, ON A CLAIM THAT WAS FALSE. It read:
    // "Matching the site's preview means one note at full velocity reaches full
    // scale -- that IS the preview's level", and rejected a -12 dB default as
    // one that "quietly contradicts the audition the user just heard". Both
    // halves are backwards. The claim was parity with renderFaustGenome's
    // OUTPUT BUFFER, and the web app never plays that buffer at its own peak: a
    // card's PLAY button puts it through 0.25 (usePreviewAudio's
    // DEFAULT_PREVIEW_VOLUME), then an 0.8 master, then a DynamicsCompressor
    // at -24/30/12. So 0 dB was levelled to a number nobody hears, and the
    // comment asserted an outcome this layer does not observe.
    //
    // MEASURED, 2026-09-13, by render-socket's measure-audition-parity.js over
    // 12 gate0 elites spanning 26 dB of raw peak: the plugin is 9.0 dB louder
    // than the card's PLAY button, median, with a spread of 0.7 dB
    // peak-to-peak. Both rules pre-registered before the run passed -- one
    // constant fixes it, and the difference does not depend on how loud the
    // genome is.
    //
    // NOT kFaustCeiling, which was the first proposal and is structurally
    // unable to carry this. trim is min(1, kFaustCeiling / reference), so
    // lowering the ceiling moves only genomes whose reference EXCEEDS it; half
    // that table sits below 0.354 and would have been left exactly where it
    // was. Attenuate-only is the point of that clamp. setOutputLevel applies
    // unconditionally and lands before the limiter, which is what this needs.
    //
    // -9 dB also lifts a single note off the 0.95 limiter, which every trimmed
    // genome previously rode on every key.
    //
    // The RANGE still reaches +6, so someone who wants the old behaviour has
    // it. The conflict the parameter exists for is real -- one note at the
    // audition level leaves room for roughly three, not for a held chord of
    // eight -- and a control is still the right answer to it. Only the default
    // was wrong.
    addParameter (outputLevelDb_ = new juce::AudioParameterFloat (
        juce::ParameterID { "output", 1 },
        "Output",
        juce::NormalisableRange<float> { -36.0f, 6.0f, 0.1f },
        -9.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // EXPERIMENT, last so it cannot disturb the indices above. With legacy
    // param ids the AU id IS the index, so adding here leaves Attack..Output at
    // 0..4 and any automation on them intact.
    //
    // It does nothing but rename itself to the first slider the loaded genome
    // exposes. See RenamableParameter for why that question comes before
    // building a pool of eight.
    addParameter (macro1_ = new RenamableParameter (
        juce::ParameterID { "macro1", 1 }, "Macro 1",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.0f));

    // One log file per host process AND per plug-in format.
    //
    // The host name alone was the original scheme, and it is not enough. Two
    // reasons, the first measured on a device:
    //
    //   - ON iOS IT IDENTIFIES NOTHING. hostApplicationPath is
    //     _NSGetExecutablePath() (juce_Files_mac.mm:238), the CURRENT process's
    //     executable. On desktop the plug-in lives in the host's process, so
    //     that is Logic or Live and the scheme works. An AUv3 runs in its OWN
    //     process, so it resolves to our appex -- and getFileNameWithoutExtension()
    //     then strips ".is" from "Synth.is", leaving "Synth". Every surface in
    //     every host wrote one "stage0-Synth.log".
    //     See AUV3_IOS_FINDINGS.md C3 and C12.
    //
    //   - ON DESKTOP IT COLLIDES TOO, latently and unnoticed: our AU and our
    //     VST3 loaded into the same host are one process writing one filename.
    //
    // hostName is kept, because where the plug-in really does share the host's
    // process it answers the actual Stage 0 question -- does this behave
    // differently in Logic vs Live vs Reaper vs Bitwig. It is simply no longer
    // trusted on its own.
    const auto hostName = juce::File::getSpecialLocation (
                              juce::File::hostApplicationPath).getFileNameWithoutExtension();

    logFile_ = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Logs/Synth.is")
                   .getChildFile ("stage0-" + hostName + "-"
                                  + juce::AudioProcessor::getWrapperTypeDescription (wrapperType)
                                  + ".log");

    logFile_.getParentDirectory().createDirectory();

    // library= says which branch InstrumentLibrary::root() resolved through.
    // On iOS "appGroup" is the shared container the app and the AUv3 both read;
    // "privateContainer" means the App Group entitlement did NOT resolve and
    // this process is looking at its own Documents -- which presents as an
    // empty instrument list and is otherwise indistinguishable from a user who
    // has downloaded nothing. Logged once here so the two are never the same
    // silence.
    //
    // THE PATH, not only the origin. `library=appGroup` says which branch
    // rootDir() took; it does not say where the files are. On 2026-09-15 the
    // UI listed fifteen instruments while devicectl found no Instruments
    // directory in the app container, the appex container OR the App Group
    // container -- and the origin name was all the log had to offer, so the
    // question could not be answered without a rebuild. It can now.
    //
    // Also the count, because a resolvable path holding nothing and an
    // unresolvable path are different failures that look identical in a UI
    // showing an empty list.
    const auto libRoot = InstrumentLibrary::root();

    logEvent ("processor_constructed host=" + hostName
              + " wrapper=" + juce::AudioProcessor::getWrapperTypeDescription (wrapperType)
              + " library=" + InstrumentLibrary::rootOriginName()
              + " root=" + libRoot.getFullPathName()
              + " exists=" + (libRoot.isDirectory() ? "yes" : "no")
              + " entries=" + juce::String (libRoot.isDirectory()
                                            ? libRoot.getNumberOfChildFiles (juce::File::findFilesAndDirectories)
                                            : -1));

   #if JUCE_IOS
    // THE CONTAINER APP'S HALF of single-sound delivery (C41).
    //
    // The appex cannot write to this app's Documents, so a render saved from
    // AUM lands in <AppGroup>/Renders and waits. Opening Synth.is moves it into
    // Documents/Renders, where Files.app shows it and any iOS DAW can import
    // it. See RenderOutbox.h for why this MOVES rather than copies.
    //
    // wrapperType, not a compile-time switch: the appex is built from this same
    // source and its Documents is a different, invisible folder. Running this
    // there would scatter renders into a sandbox nobody opens -- and it would
    // look like it worked.
    //
    // AT LAUNCH ONLY, which is the staleness this design is being tried out to
    // measure: a render made in AUM while Synth.is is already open in the
    // background does not appear until the app is next launched cold. If that
    // turns out to feel wrong, the answer is a document picker from the appex
    // rather than more sync here.
    if (wrapperType == wrapperType_Standalone)
    {
        // Off the message thread. This is file I/O of unknown size on app
        // launch, and the one thing worse than a late render is a launch that
        // visibly stalls before the window appears.
        juce::Thread::launch ([this, weak = std::weak_ptr<int> (lifetime_)]
        {
            const auto drained = kromosynth::renders::drain();

            // Silence when there was nothing to do. A line every launch saying
            // zero would bury the launches where it did something.
            if (drained.moved == 0 && drained.failed == 0)
                return;

            juce::MessageManager::callAsync ([this, weak, drained]
            {
                if (weak.expired())
                    return;

                logEvent ("renders_drained moved=" + juce::String (drained.moved)
                          + " failed=" + juce::String (drained.failed)
                          + " first=" + (drained.firstName.isNotEmpty()
                                             ? drained.firstName.quoted()
                                             : juce::String ("none"))
                          + " to=" + kromosynth::renders::deliveredRoot().getFullPathName());
            });
        });
    }
   #endif

    // Two per second, and it writes nothing unless a MIDI counter moved. Slow
    // enough to be free, fast enough that a burst of notes is not averaged
    // into one line.
    startTimer (500);

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
        [this, weak = std::weak_ptr<int> (lifetime_)] (juce::String artifactId,
                                                       bool ok,
                                                       kromosynth::acquisition::Verb verb)
    {
        if (weak.expired() || ! ok || artifactId.isEmpty()) return;

        // A RENDER IS NOT AN INSTRUMENT. It is a file in the outbox; there is no
        // library directory under this id and nothing to make active. Logged
        // rather than silently skipped, so "I saved a render and the instrument
        // did not change" is answerable from the log instead of from the source.
        if (verb == kromosynth::acquisition::Verb::render)
        {
            logEvent ("render_saved id=" + artifactId);
            return;
        }

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

namespace
{

/** The current process id, for attributing lines that share a file. */
int currentProcessId()
{
   #if JUCE_MAC || JUCE_IOS || JUCE_LINUX
    return (int) ::getpid();
   #else
    return 0;
   #endif
}

/** Appends one line with ONE write(2) on an O_APPEND descriptor.

    NOT juce::File::appendText, which is what this used to be, and the
    difference is correctness rather than speed. appendText builds a
    FileOutputStream, and FileOutputStream::openHandle on POSIX opens with
    O_CREAT | O_RDWR -- no O_APPEND (juce_SharedCode_posix.h:581) -- then seeks
    to the end and writes at the offset it just remembered. Two processes each
    seek to EOF and then write THERE, so the second can land on top of the
    first's bytes. That is LOST lines, not merely interleaved ones.

    Live rather than theoretical: an AUv3's container is per extension, not per
    host, so every instance in every host shares one file and each instance is
    its own process. AUV3_IOS_FINDINGS.md C12.

    O_APPEND advances the offset and writes under the same inode lock, so one
    short write lands whole.
*/
void appendLineAtomically (const juce::File& file, const juce::String& line)
{
   #if JUCE_MAC || JUCE_IOS || JUCE_LINUX
    const auto* bytes = line.toRawUTF8();
    const auto length = std::strlen (bytes);

    // 4096 is the smallest PIPE_BUF any POSIX system promises. Past it a single
    // write is no longer guaranteed to land whole, and a TORN line is worse
    // than an out-of-order one -- it would be read as corruption rather than as
    // concurrency. So long lines go back to JUCE and keep the race, which is at
    // least the behaviour that was there before.
    if (length > 0 && length < 4096)
    {
        const int fd = ::open (file.getFullPathName().toRawUTF8(),
                               O_WRONLY | O_APPEND | O_CREAT, 0644);

        if (fd >= 0)
        {
            const auto written = ::write (fd, bytes, length);
            ::close (fd);
            juce::ignoreUnused (written);
            return;
        }
    }
   #endif

    file.appendText (line, false, false, "\n");
}

} // namespace

void KromosynthAudioProcessor::logEvent (const juce::String& event)
{
    // Timestamped, append-only, flushed per line. Slow and completely
    // acceptable: nothing here runs on the audio thread.
    //
    // pid= because the FILENAME now separates hosts and formats but not
    // INSTANCES. Two AUv3s in one AUM session are two processes sharing one
    // file, and without this a working instance and a failing one are
    // indistinguishable once their lines interleave.
    const auto line = juce::Time::getCurrentTime().toISO8601 (true)
                      + "  pid=" + juce::String (currentProcessId())
                      + "  " + event + "\n";
    appendLineAtomically (logFile_, line);
    DBG ("[stage0] " << event);
}

KromosynthAudioProcessor::~KromosynthAudioProcessor()
{
    // FIRST. juce::Timer's own destructor would stop it, but that runs after
    // this class's members are gone -- and timerCallback reads logFile_ and the
    // MIDI atomics. Stopping here closes the window rather than relying on
    // base-class destruction order.
    stopTimer();

    // Unconditional, and before the engine-specific stats: this is the line
    // that says whether the OS ever pulled audio, which is a different question
    // from whether an engine rendered any.
    logEvent ("audio_callback blocks=" + juce::String (blocksSeen()) + " reason=destructor");

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

    // Engine independent, because a load figure is meaningful whatever is or is
    // not loaded -- "nothing loaded and still at 40%" is worth seeing.
    //
    // exchange, not load: blockLoadRecent_ is a METER. Reading it clears it, so
    // each poll reports the worst block since the previous poll rather than the
    // worst since the plugin opened. const_cast because engineState() is const
    // and this is the one field it deliberately mutates.
    st.loadRecent = const_cast<std::atomic<float>&> (blockLoadRecent_)
                        .exchange (0.0f, std::memory_order_relaxed);
    st.loadMax    = blockLoadMax_.load (std::memory_order_relaxed);
    st.blocksOver = blocksOverBudget_.load (std::memory_order_relaxed);

    // THE FRAMES ACTUALLY DELIVERED, not getBlockSize().
    //
    // This read getBlockSize(), the host's declared maximum, while loadRecent
    // and loadMax beside it are fractions of the ACTUAL block. AUM declares
    // 2048 and delivers 256, so the editor showed "load 35% of 42.67 ms" for a
    // block whose real deadline was 5.33 ms -- a correct percentage next to a
    // denominator eight times too large, which is worse than either alone.
    //
    // Falls back to getBlockSize() only before the first block, where there is
    // no measured figure and the declared one is the best available.
    if (const auto rate = getSampleRate(); rate > 0.0)
    {
        const auto frames = lastBlockFrames_.load (std::memory_order_relaxed);
        st.budgetMs = (float) (1000.0 * (frames > 0 ? frames : getBlockSize()) / rate);
    }

   #if KROMOSYNTH_HAS_FAUST
    if (faust_ != nullptr && faust_->isLoaded())
    {
        st.faust = true;
        st.notesOn = faust_->notesOn();
        st.notesOff = faust_->notesOff();
        st.activeVoices = faust_->activeVoices();
        st.forcedStops = faust_->forcedStops();
        const auto starts = faust_->voiceStarts();
        st.startsIdle = starts.idle;
        st.startsReused = starts.reused;
        st.startsStolen = starts.stolen;
        st.trimDb = 20.0f * std::log10 (juce::jmax (1.0e-9f, faustTrim_));
        st.rawPeak = faustRawPeak_;
        st.rtf = (float) faustRtf_;
        st.voiceCap = faustVoices_;

        // FaustEngine has exposed both of these since §4.2 -- "a synth that
        // renders silence never misses a deadline, never crashes, and passes
        // every validator" -- and until now nothing called either accessor.
        // The guard existed and reported to no one.
        st.renderPeak = faust_->peakSinceReset();
        st.renderBlocks = faust_->blocksRendered();
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

void KromosynthAudioProcessor::triggerTestNote (int midiNote, float velocity, int durationMs)
{
    JUCE_ASSERT_MESSAGE_THREAD

    midiNote   = juce::jlimit (0, 127, midiNote);
    velocity   = juce::jlimit (0.0f, 1.0f, velocity);
    durationMs = juce::jlimit (10, 10000, durationMs);

    // Queued for the next block. The note-off follows on a delay through the
    // message thread rather than being queued now with an offset: a MidiBuffer
    // event offset has to fall inside the block it is drained into, and a test
    // note that never releases is a stuck note -- the exact failure panic()
    // exists to clean up after.
    {
        const juce::ScopedLock sl (testNoteLock_);
        pendingTestNotes_.addEvent (juce::MidiMessage::noteOn (1, midiNote, velocity), 0);
    }

    juce::Timer::callAfterDelay (durationMs,
                                 [this, weak = std::weak_ptr<int> (lifetime_), midiNote]
    {
        if (weak.expired()) return;

        const juce::ScopedLock sl (testNoteLock_);
        pendingTestNotes_.addEvent (juce::MidiMessage::noteOff (1, midiNote), 0);
    });

    logEvent ("test_note note=" + juce::String (midiNote)
              + " vel=" + juce::String (velocity, 2)
              + " ms=" + juce::String (durationMs)
              + " active=" + juce::String (active_ == Active::none ? "none"
                                           : (active_ == Active::sampler ? "sampler" : "faust"))
              + " blocks_so_far=" + juce::String (blocksSeen()));
}

void KromosynthAudioProcessor::timerCallback()
{
    // FIRST, and deliberately ABOVE the MIDI gate below.
    //
    // The transient this was built to catch is the first block after the
    // suspend barrier lifts on a load, and no MIDI moved to produce it -- so a
    // drain placed after that early return would never fire for the one case it
    // exists for. It writes nothing when there is nothing new, so calling it
    // unconditionally does not reintroduce the idle chatter the gate prevents.
    drainOverBudgetBlocks();

    const auto events = midiEvents_.load (std::memory_order_relaxed);

    // Nothing new since the last line. An idle plugin must not fill the log,
    // because the log is the only diagnostic surface on a phone.
    if (events == midiLoggedAt_)
        return;

    midiLoggedAt_ = events;

    logEvent ("midi_in events=" + juce::String (events)
              + " on=" + juce::String (midiNoteOns_.load (std::memory_order_relaxed))
              + " off=" + juce::String (midiNoteOffs_.load (std::memory_order_relaxed))
              // Non-zero here means note-on and its note-off arrived in ONE
              // block: a zero-length note, silent whatever the engine does.
              + " zero_length=" + juce::String (midiZeroLength_.load (std::memory_order_relaxed))
              + " last_note=" + juce::String (midiLastNote_.load (std::memory_order_relaxed))
              // Offset of that note within its block. A value far from 0 with a
              // 64-frame block would mean the messages are being placed wrong
              // rather than delivered late.
              + " last_offset=" + juce::String (midiLastOffset_.load (std::memory_order_relaxed))
              + " max_per_block=" + juce::String (midiMaxPerBlock_.load (std::memory_order_relaxed))
              + " blocks=" + juce::String (blocksSeen())
              // The pair that makes silence readable. render_peak=0 with
              // render_blocks large is an instrument that rendered NOTHING
              // audible; render_blocks=0 is a host that never asked us to
              // render. Without both, those are the same silence -- exactly the
              // distinction sampler_render_stats was given for the sampler and
              // the Faust path never had. Logged here rather than on teardown
              // because it fires when MIDI moves, which is when the question
              // "I played and heard nothing" actually gets asked.
              + faustRenderStatsForLog()
              // The deadline reading, alongside the MIDI that provoked it. load
              // is a fraction of the block period: 0.08 is eight per cent of
              // the time available, 1.0 is all of it. over= counts blocks that
              // consumed the whole budget, and is only meaningful against
              // blocks= above.
              + " load_max=" + juce::String (blockLoadMax_.load (std::memory_order_relaxed), 3)
              + " over=" + juce::String (blocksOverBudget_.load (std::memory_order_relaxed))
              // ACTUAL frames, not getBlockSize(). Same defect as engineState
              // had: the host's declared maximum is not the deadline the load
              // fraction was computed against. frames= is printed alongside so
              // a host that varies its block size is visible rather than
              // averaged into one misleading number.
              + " frames=" + juce::String (lastBlockFrames_.load (std::memory_order_relaxed))
              + " budget_ms=" + juce::String (
                    (getSampleRate() > 0.0 && lastBlockFrames_.load (std::memory_order_relaxed) > 0)
                        ? 1000.0 * lastBlockFrames_.load (std::memory_order_relaxed) / getSampleRate()
                        : 0.0, 2)
              // THE CAP QUESTION, on one line. cap= is what the plugin decided
              // the polyphony should be; load_by_voices= is what each voice
              // count actually cost. The bar in C29 is applied by reading the
              // entry at cap= and checking its block count is at least 200.
              //
              // Here rather than in a new event, because this line already
              // fires exactly when MIDI moves -- which is when voices are
              // sounding and the table has anything to say.
              + faustVoiceCapForLog()
              + loadByVoicesForLog());
}

void KromosynthAudioProcessor::recordBlockLoad (int frames, std::int64_t ticks) noexcept
{
    const auto rate = getSampleRate();

    // Before prepareToPlay there is no deadline to be a fraction of. Silently
    // skipping is right: a made-up denominator would produce a plausible number.
    if (frames <= 0 || rate <= 0.0 || ticks < 0)
        return;

    const double elapsed = (double) ticks
                         / (double) juce::Time::getHighResolutionTicksPerSecond();
    const double budget  = (double) frames / rate;
    const auto   load    = (float) (elapsed / budget);

    // Published so the editor can report the deadline this load was measured
    // against, rather than getBlockSize()'s declared maximum. See the member.
    lastBlockFrames_.store (frames, std::memory_order_relaxed);

    // Single writer, so load-then-store rather than a compare-exchange loop.
    // blockLoadRecent_ has a second writer -- engineState() clears it on read --
    // and the race there can only lose one sample of a meter that refreshes
    // twice a second. Worth stating rather than pretending it is airtight.
    if (load > blockLoadRecent_.load (std::memory_order_relaxed))
        blockLoadRecent_.store (load, std::memory_order_relaxed);

    if (load > blockLoadMax_.load (std::memory_order_relaxed))
        blockLoadMax_.store (load, std::memory_order_relaxed);

    // ── load at THIS voice count ────────────────────────────────────
    //
    // The voice count is now read on EVERY block, not only on overruns, and
    // that is a new cost on the ordinary path -- so it is stated rather than
    // buried. A bounded loop over at most 32 non-atomic idle flags, about 190
    // times a second at AUM's 5.33 ms delivery. No allocation, no lock, no
    // syscall, and this thread is the only writer of those flags outside the
    // suspend window.
    //
    // Worth paying because the alternative is inferring the per-voice slope
    // from one undifferentiated maximum, which is exactly the inference C27 was
    // written to stop.
    int voices = -1;
   #if KROMOSYNTH_HAS_FAUST
    if (faust_ != nullptr) voices = faust_->activeVoices();
   #endif

    const auto sinceLoad = blocksSinceLoad_.fetch_add (1, std::memory_order_relaxed);

    if (voices >= 0 && sinceLoad >= kLoadTableWarmupBlocks)
    {
        const auto v = (size_t) juce::jlimit (0, kMaxTrackedVoices, voices);

        // The distribution. C30: a maximum alone measures the scheduling tail,
        // so the shape is what the cap has to be judged from.
        const auto bucket = (size_t) juce::jlimit (0, kLoadBuckets - 1,
                                                   (int) (load * 10.0f));
        loadHistogram_[v][bucket].fetch_add (1, std::memory_order_relaxed);

        if (load > loadByVoices_[v].load (std::memory_order_relaxed))
            loadByVoices_[v].store (load, std::memory_order_relaxed);
    }

    // >= 1.0 is the deadline consumed entirely. Whether the host actually
    // glitched depends on its own buffering, so this counts OUR overruns and
    // does not claim a dropout was heard.
    if (load < 1.0f)
        return;

    blocksOverBudget_.fetch_add (1, std::memory_order_relaxed);

    // ── the circumstances, because the count alone answers nothing ──────────
    //
    // See kOverBudgetCaptures. claimed is incremented whether or not a slot is
    // free, so a window with 200 overruns and 8 kept says 200 rather than
    // looking like 8.
    const auto slot = overBudgetClaimed_.fetch_add (1, std::memory_order_relaxed);

    if (slot >= (std::uint32_t) kOverBudgetCaptures)
        return;

    auto& rec = overBudgetBlocks_[slot];
    rec.ticks     = juce::Time::getHighResolutionTicks();
    rec.load      = load;
    rec.elapsedMs = (float) (elapsed * 1000.0);
    rec.budgetMs  = (float) (budget * 1000.0);
    rec.frames    = frames;

    // Already incremented for THIS block at the top of processBlock, so this is
    // one-based and includes the block being recorded. block=3 is the third
    // block the host ever asked for, which is the reading that says "cold".
    rec.blockIndex = blocksSeen_.load (std::memory_order_relaxed);

    // THE discriminating field, and now read ONCE per block above rather than
    // again here. 142% with nothing sounding is a transient by construction --
    // there was no synthesis to be slow. 142% with three voices is the genome,
    // and that is a different decision.
    //
    // Sampled at the END of the block, because this runs from the RAII guard's
    // destructor after the engine has returned. A voice that released partway
    // through is therefore attributed to the lower count, which overstates
    // per-voice cost -- the safe direction to be wrong in when the question is
    // whether a cap is too generous.
    rec.voices = juce::jmax (0, voices);

    // LAST, and the only ordered write in the function. Everything above is a
    // plain store into a slot no other thread reads until this publishes it.
    overBudgetPublished_.fetch_add (1, std::memory_order_release);
}

void KromosynthAudioProcessor::drainOverBudgetBlocks()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // acquire, pairing with the release in recordBlockLoad: every slot below
    // this count is fully written and visible here.
    const auto published = overBudgetPublished_.load (std::memory_order_acquire);

    if (published == overBudgetDrained_)
        return;

    const auto claimed    = overBudgetClaimed_.load (std::memory_order_relaxed);
    const auto nowTicks   = juce::Time::getHighResolutionTicks();
    const auto perSecond  = (double) juce::Time::getHighResolutionTicksPerSecond();

    for (auto i = overBudgetDrained_; i < published; ++i)
    {
        const auto& rec = overBudgetBlocks_[i];

        // WALL CLOCK, reconstructed rather than taken now. The audio thread can
        // read a monotonic tick count and cannot format a date, and this line is
        // written up to 500 ms after the block it describes -- so a drain-time
        // timestamp would place a load transient half a second away from the
        // instrument_load line it belongs to, and break the one correlation
        // this whole mechanism exists to make.
        const auto ageMs = 1000.0 * (double) (nowTicks - rec.ticks) / perSecond;
        const auto at    = juce::Time::getCurrentTime()
                         - juce::RelativeTime::milliseconds ((juce::int64) ageMs);

        // elapsed_ms as well as load, because 142% of 42.67 ms is a 60 ms block
        // and no reader should have to do that multiplication to see it.
        logEvent ("block_over_budget n=" + juce::String (i + 1)
                  + "/" + juce::String (kOverBudgetCaptures)
                  + " at=" + at.toISO8601 (true)
                  + " age_ms=" + juce::String (ageMs, 1)
                  + " load=" + juce::String (rec.load, 3)
                  + " elapsed_ms=" + juce::String (rec.elapsedMs, 2)
                  + " budget_ms=" + juce::String (rec.budgetMs, 2)
                  + " frames=" + juce::String (rec.frames)
                  + " block=" + juce::String (rec.blockIndex)
                  + " voices=" + juce::String (rec.voices)
                  + " over_since_load=" + juce::String (claimed));
    }

    overBudgetDrained_ = published;
}

void KromosynthAudioProcessor::rearmLoadMeasurement()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // BEFORE resetting, so an overrun captured under the previous instrument is
    // logged rather than discarded by the load that replaced it. Same thread as
    // the timer's own drain, so the two cannot interleave.
    drainOverBudgetBlocks();

    // Writing counters the audio thread also writes is safe ONLY because the
    // caller holds the suspend window: processBlock cannot be in flight, and no
    // later block can enter until the barrier lifts. The CriticalSection both
    // sides take supplies the happens-before edge, so relaxed is enough here.
    overBudgetClaimed_.store (0, std::memory_order_relaxed);
    overBudgetPublished_.store (0, std::memory_order_relaxed);
    overBudgetDrained_ = 0;

    // The table too, and the warmup counter with it. A table carried across a
    // load would average this genome's cost with the last one's and there would
    // be nothing in the reading to reveal it.
    for (int v = 0; v <= kMaxTrackedVoices; ++v)
    {
        loadByVoices_[(size_t) v].store (0.0f, std::memory_order_relaxed);

        for (int b = 0; b < kLoadBuckets; ++b)
            loadHistogram_[(size_t) v][(size_t) b].store (0, std::memory_order_relaxed);
    }

    blocksSinceLoad_.store (0, std::memory_order_relaxed);
}

juce::String KromosynthAudioProcessor::faustVoiceCapForLog() const
{
   #if KROMOSYNTH_HAS_FAUST
    if (faust_ != nullptr && faust_->isLoaded())
        return " cap=" + juce::String (faustVoices_);
   #endif
    return {};
}

juce::String KromosynthAudioProcessor::loadByVoicesForLog() const
{
    juce::String out;

    for (int v = 0; v <= kMaxTrackedVoices; ++v)
    {
        std::uint32_t counts[kLoadBuckets];
        std::uint32_t n = 0;

        for (int b = 0; b < kLoadBuckets; ++b)
        {
            counts[b] = loadHistogram_[(size_t) v][(size_t) b].load (std::memory_order_relaxed);
            n += counts[b];
        }

        // Never seen, so omitted. 33 rows of zeroes would bury the three that
        // carry the answer.
        if (n == 0)
            continue;

        // Bucket 10 is [1.0, 1.1), so 10 and above IS over budget. Derived
        // from the same histogram rather than counted separately -- two
        // counters for one quantity is two things that can disagree, which is
        // how C26's ladder went unnoticed in three places at once.
        std::uint32_t over = 0;
        for (int b = 10; b < kLoadBuckets; ++b) over += counts[b];

        // Reported as the bucket's LOWER EDGE, so p50=0.6 means the median
        // block fell in [0.6, 0.7). Coarse and stated, rather than a
        // false-precision interpolation across a bucket nothing was measured
        // inside.
        //
        // CEIL, AND AT LEAST 1. Written first as (uint32)(q * n), which is 0
        // whenever q * n < 1 -- and `seen >= 0` is true at the first bucket, so
        // a single block at 0.8 reported p50=0.0. Wrong precisely when the
        // sample is thin, which is the case the bar most needs to distrust.
        // Caught by replicating this lambda outside the plugin and testing it
        // against hand-worked cases, not by reading it.
        const auto quantile = [&counts, n] (double q)
        {
            const auto target = juce::jmax ((std::uint32_t) 1,
                                            (std::uint32_t) std::ceil (q * (double) n));
            std::uint32_t seen = 0;

            for (int b = 0; b < kLoadBuckets; ++b)
            {
                seen += counts[b];
                if (seen >= target) return b;
            }

            return kLoadBuckets - 1;
        };

        out << (out.isEmpty() ? " load_dist=" : " ")
            << juce::String (v) << ":n=" << juce::String ((int) n)
            << ",p50=" << juce::String (quantile (0.50) / 10.0, 1)
            << ",p99=" << juce::String (quantile (0.99) / 10.0, 1)
            << ",max=" << juce::String (loadByVoices_[(size_t) v].load (
                                            std::memory_order_relaxed), 3)
            << ",over=" << juce::String ((int) over);
    }

    return out;
}

juce::String KromosynthAudioProcessor::faustRenderStatsForLog() const
{
   #if KROMOSYNTH_HAS_FAUST
    if (faust_ != nullptr && faust_->isLoaded())
        return " render_peak=" + juce::String (faust_->peakSinceReset(), 6)
             + " render_blocks=" + juce::String (faust_->blocksRendered());
   #endif
    return {};
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

        // The capture window is PER INSTRUMENT, which is what removes the
        // arithmetic from the correlation: an over-budget line below this load's
        // instrument_load line is about this instrument by construction, and
        // over_since_load counts only its overruns. Inside the barrier because
        // it writes counters the audio thread also writes.
        rearmLoadMeasurement();

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
   #else
    if (resolved.kind == InstrumentLibrary::Kind::faust)
    {
        // No Faust engine in this build, and the sampler must NOT be handed a
        // .wasm. Falling through did exactly that: sfizz parsed
        // instrument.wasm as SFZ and reported "loadSfzFile returned false;
        // parse errors go to stderr" for an artefact that was never an SFZ.
        // The error named the wrong subsystem and pointed a reader at the wrong
        // file -- which is how the first instrument acquired on iOS looked like
        // a corrupt sample set rather than an unsupported engine.
        //
        // iOS is the configuration that arrives here. wasmtime cannot ship on
        // it -- its C API offers only Cranelift and Winch and both are JIT --
        // so KROMOSYNTH_BUILD_FAUST stays off until FaustEngine.cpp has a wasm3
        // backend. See the platform block in the top-level CMakeLists.
        outcome.detail = "this is a live Faust instrument, and this build has no "
                         "Faust engine";

        logEvent ("instrument_load_unsupported id=" + id + " kind=faust"
                  + " -- built without KROMOSYNTH_BUILD_FAUST, nothing was loaded");

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

    // Here as well as on the Faust path, so that over_since_load means what it
    // says whichever engine loaded last. The voice-cap question is Faust's
    // alone, but a sampler load that left the window open would make the next
    // Faust reading span two instruments while still being labelled per-load --
    // a field that lies is worse than one that is missing.
    rearmLoadMeasurement();

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

    // Any diagnostic note still queued belongs to the previous configuration.
    // Dropped rather than carried across: a note-on whose note-off was queued
    // before a rate change would be delivered without its release.
    {
        const juce::ScopedLock sl (testNoteLock_);
        pendingTestNotes_.clear();
    }

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

// Outside the Faust guard, matching where the members it reads are now
// declared. It was stranded inside it, so a sampler-only build declared the
// accessor in the header and then left it undefined at LINK time -- the one
// error remaining after the header moves, and the only one of the set that a
// compile could not have caught.
KromosynthAudioProcessor::LoadOutcome KromosynthAudioProcessor::lastLoadOutcome() const
{
    const juce::ScopedLock sl (outcomeLock_);
    return lastOutcome_;
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

    // runtimeId() names the wasm runtime LIBRARY, not this plugin's embedding of
    // it, so a build that changed the host bindings or the measurement loop
    // would otherwise read another build's rtf. Bump KROMOSYNTH_MEASURE_EPOCH
    // (CMake cache variable) when either changes.
    if ((int) get ("epoch") != KROMOSYNTH_MEASURE_EPOCH)
    {
        const_cast<KromosynthAudioProcessor*> (this)->logEvent (
            "faust_measure_cache_miss epoch=" + juce::String ((int) get ("epoch"))
            + " build_epoch=" + juce::String (KROMOSYNTH_MEASURE_EPOCH)
            + " -- another build's measurement, re-measuring");
        return std::nullopt;
    }
    if ((double) get ("sampleRate") != faustRate_) return std::nullopt;
    if ((juce::int64) get ("wasmBytes") != faustWasm_.getSize()) return std::nullopt;

    if (get ("wasmHash").toString() != hashFile (faustWasm_))
        return std::nullopt;

    FaustEngine::Measured m;
    m.peak    = (float) (double) get ("peak");
    m.rtf     = (double) get ("rtf");

    // DERIVED, not read back. The stored "capAt4x" below is deliberately
    // ignored: the cache validates runtime, sample rate and module bytes, so a
    // change to the CAP FORMULA passes every one of those checks and the cache
    // would keep serving caps computed by an older build. That is exactly what
    // happened on 2026-09-09 -- the ladder-to-arithmetic change was live in the
    // binary and had no visible effect on any already-measured instrument.
    //
    // rtf is the measurement and is worth trusting from disk. The cap is this
    // build's conclusion from it, and a conclusion must not outlive the code
    // that drew it.
    m.capAt4x = FaustEngine::capForRtf (m.rtf);
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
    o->setProperty ("epoch", KROMOSYNTH_MEASURE_EPOCH);
    o->setProperty ("sampleRate", faustRate_);
    o->setProperty ("wasmBytes", faustWasm_.getSize());
    o->setProperty ("wasmHash", hashFile (faustWasm_));

    o->setProperty ("peak", m.peak);
    o->setProperty ("rtf", m.rtf);

    // Written for human readability only -- the read path above derives it from
    // rtf and ignores this. Kept rather than dropped so a dumped cache file
    // still says what the build that wrote it concluded.
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

    // EXPERIMENT: rename the macro to the first slider this genome exposes.
    //
    // freq, gain and gate are excluded because the plugin already drives them
    // from MIDI -- they appear on 30, 24 and 29 of 30 instruments and are not
    // the genome's character. What remains is what a player would actually want
    // a knob for: lvl, cut, res, drv, att and so on.
    //
    // NOTHING IS WRITTEN to the slider. The name is the whole experiment; the
    // version that also wrote was reverted for measuring an instrument after
    // altering it.
    if (macro1_ != nullptr)
    {
        juce::String first;

        for (const auto& p : faust_->parameters())
        {
            if (p.label == "freq" || p.label == "gain" || p.label == "gate") continue;
            first = p.label;
            break;
        }

        if (auto* renamable = dynamic_cast<RenamableParameter*> (macro1_))
            renamable->setDisplayName (first.isEmpty() ? "Macro 1" : first, *this);

        // WHAT WAS ASKED FOR, AND NOTHING ELSE. This used to also log
        // macro1_->getName(32) as `reported_as`, which is asking JUCE what it
        // called the parameter immediately after telling JUCE what to call it.
        // It agreed every time -- 149 matches on the phone, more on desktop
        // back to 2026-09-05 -- and proved nothing at all.
        //
        // The question is whether the HOST picked the new name up, and that is
        // not observable from in here: hosts cache the parameter list at
        // construction and many ignore later changes. Björn reports seeing a
        // parameter still called "macro" in AUM, which is the actual evidence
        // and points the other way.
        //
        // So this records the intent and stops implying it was verified. The
        // reading that settles it is done by LOOKING AT THE HOST: open AUM's
        // parameter list, load a Faust genome declaring a slider like "att",
        // and see whether the name changes. If it does not, renaming is dead
        // and a fixed pool of Macro 1..8 with the mapping shown in our own UI
        // is the design.
        logEvent ("macro_rename_requested to=\"" + (first.isEmpty() ? juce::String ("Macro 1") : first)
                  + "\" (host-side result NOT observable here)");
    }

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
   #if KROMOSYNTH_PROCESSOR_PROBE
    // tools/processor-probe only. capAt4x comes from a timed render, so it moves
    // with machine load and two renders of one build would differ for reasons
    // that are not the code. Not compiled into the shipped targets.
    if (const auto pinned = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_VOICES", {});
        pinned.isNotEmpty())
        faustVoices_ = juce::jlimit (1, 32, pinned.getIntValue());
   #endif
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
    logEvent ("audio_callback blocks=" + juce::String (blocksSeen())
              + " reason=releaseResources");

   #if KROMOSYNTH_HAS_SAMPLER
    logRenderStats ("releaseResources");
   #endif
}

void KromosynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Counted BEFORE any routing, and deliberately not inside an engine.
    //
    // sampler_render_stats reports engine_->stats(), whose counters only move
    // inside engine_->process() -- which this function skips whenever
    // active_ == Active::none. So "blocks=0" from that line means EITHER the
    // host never pulled audio OR nothing was loaded, and those are the two
    // possibilities anyone debugging silence needs to tell apart. On the first
    // iOS run it read blocks=0 and could not answer whether the phone was
    // rendering at all.
    //
    // Relaxed, and on the audio thread on purpose: one increment per block on a
    // counter nobody reads in the same breath is free, and a fetch_add on an
    // uncontended atomic is not a syscall.
    blocksSeen_.fetch_add (1, std::memory_order_relaxed);

    // RAII, because processBlock has THREE exits: the sampler path returns, the
    // Faust path returns, and the no-engine path falls through to
    // buffer.clear(). Timing at any one of them would silently omit the others,
    // and the omitted case would be indistinguishable from a block that cost
    // nothing.
    //
    // A local class inside a member function has the enclosing function's access
    // rights, so this can call the private recorder without widening it.
    struct BlockLoadTimer
    {
        KromosynthAudioProcessor& proc;
        int frames;
        std::int64_t startedAt = juce::Time::getHighResolutionTicks();

        ~BlockLoadTimer()
        {
            proc.recordBlockLoad (frames, juce::Time::getHighResolutionTicks() - startedAt);
        }
    } blockLoadTimer { *this, buffer.getNumSamples() };

    // Diagnostic notes merged into the HOST's buffer, before any routing, so
    // they reach whichever engine is active by the same path a real note takes.
    //
    // TRY lock: the audio thread must never wait on the message thread, and a
    // diagnostic note arriving one block later is not a defect worth a priority
    // inversion. Empty on every block in normal use.
    {
        const juce::ScopedTryLock sl (testNoteLock_);

        if (sl.isLocked() && ! pendingTestNotes_.isEmpty())
        {
            midiMessages.addEvents (pendingTestNotes_, 0, buffer.getNumSamples(), 0);
            pendingTestNotes_.clear();
        }
    }

    // ── MIDI arrival, counted before any engine sees it ─────────────────────
    //
    // Skipped entirely on the empty buffer, which is every block when nobody is
    // playing, so the scan costs nothing in the normal case.
    if (! midiMessages.isEmpty())
    {
        int inThisBlock = 0;
        bool onSeen[128] = {};

        for (const auto meta : midiMessages)
        {
            const auto m = meta.getMessage();
            ++inThisBlock;

            if (m.isNoteOn())
            {
                midiNoteOns_.fetch_add (1, std::memory_order_relaxed);
                midiLastNote_.store (m.getNoteNumber(), std::memory_order_relaxed);
                midiLastOffset_.store (meta.samplePosition, std::memory_order_relaxed);
                onSeen[m.getNoteNumber() & 127] = true;
            }
            else if (m.isNoteOff())
            {
                midiNoteOffs_.fetch_add (1, std::memory_order_relaxed);

                // Its own note-on was in this same block, so the note has zero
                // length and cannot sound. THE signature to look for: it would
                // explain silence on notes that were definitely pressed.
                if (onSeen[m.getNoteNumber() & 127])
                    midiZeroLength_.fetch_add (1, std::memory_order_relaxed);
            }
        }

        midiEvents_.fetch_add ((std::uint64_t) inThisBlock, std::memory_order_relaxed);

        if (inThisBlock > midiMaxPerBlock_.load (std::memory_order_relaxed))
            midiMaxPerBlock_.store (inThisBlock, std::memory_order_relaxed);
    }

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
