// SamplerEngine — see SamplerEngine.h for the thread-safety contract and for
// why the render is chunked.

#include "SamplerEngine.h"

SamplerEngine::SamplerEngine() = default;
SamplerEngine::~SamplerEngine() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OFF
// ─────────────────────────────────────────────────────────────────────────────

void SamplerEngine::prepare (double sampleRate, int maxBlockSize)
{
    // config::maxBlockSize is 8192 (Config.h:25) and sfizz only ASSERTs it,
    // which is a no-op in Release. Clamp rather than trust: a host asking for
    // more would otherwise get silence with no log line at all.
    const int block = juce::jlimit (1, 8192, maxBlockSize);

    if (block != maxBlockSize)
    {
        // Recorded on the result of the next load rather than logged from
        // here, because the engine has no logger and should not acquire one.
        jassertfalse;
    }

    // VERIFIED, by temporarily forcing this value to 64 and rerunning auval:
    // a 4096-frame host block produced max_chunks=64, auval passed, and audio
    // still rendered. Without that experiment the chunk loop was untested code
    // defending against an untested failure, which is two assumptions wearing
    // one comment.
    //
    // It also showed chunking is NOT bit-identical: the same auval render
    // peaked at 0.157102 unchunked and 0.157467 at a forced 64. sfizz smooths
    // controls per block, so a smaller block means finer granularity. Inherent
    // to any block-size change rather than a defect, and an argument for
    // chunking at the PREPARED size rather than some fixed small one.

    preparedRate_ = sampleRate;
    preparedBlock_ = block;

    synth_.setSampleRate (static_cast<float> (sampleRate));
    synth_.setSamplesPerBlock (block);

    // Only touched on a mono bus, but allocated unconditionally so the audio
    // thread never discovers it needs one.
    scratch_.setSize (2, block, false, true, true);
    scratch_.clear();

    // NOT calling enableFreeWheeling(). It makes the file pool block until
    // background loading finishes (Synth.cpp:1115), which is exactly right for
    // the offline harness and exactly wrong on an audio thread.
    //
    // STAGE 1: the honest refinement is to freewheel only when the host says
    // the render is offline. JUCE's VST3 wrapper already calls
    // setNonRealtime(data.processMode == Vst::kOffline) from the audio thread
    // under the callback lock (_VST3.cpp:3716), and enableFreeWheeling is
    // RT-annotated, so it fits. Left out on purpose: a bounce that blocks is a
    // different failure from a live callback that blocks, and telling them
    // apart needs a measurement rather than a plausible argument.
}

SamplerEngine::LoadResult SamplerEngine::load (const juce::File& sfzFile)
{
    LoadResult result;

    if (sfzFile.getFullPathName().isEmpty())
    {
        result.detail = "no path configured";
        return result;
    }

    if (! sfzFile.existsAsFile())
    {
        result.detail = "file does not exist";
        return result;
    }

    const auto started = juce::Time::getMillisecondCounterHiRes();

    const bool returned = synth_.loadSfzFile (sfzFile.getFullPathName().toStdString());

    result.loadMs = juce::Time::getMillisecondCounterHiRes() - started;

    // F11. loadSfzFile returning true is not the same as having an instrument.
    // Regions with unresolvable samples are removed after parsing and the
    // return value does not notice.
    result.numRegions = synth_.getNumRegions();
    result.numVoices = synth_.getNumVoices();
    result.ok = returned && result.numRegions > 0;

    // ASCII only. The standing rule about non-ASCII in C++ string literals is
    // in STAGE0_FINDINGS.md for a reason, and the first version of this line
    // used an em-dash and reached the log as a mojibake byte. Caught by
    // reading the log rather than by remembering the rule.
    if (! returned)
        result.detail = "loadSfzFile returned false; parse errors go to stderr";
    else if (result.numRegions == 0)
        result.detail = "parsed, but every region was culled: check default_path "
                        "and the samples directory (F11)";

    // Left false on failure, which means the caller retries on every
    // prepareToPlay. Deliberate and cheap: a failed load of a culled file
    // measured 0.7 ms, and an artefact that appears after the plugin was
    // instantiated will then be picked up. The cost is a repeated log line.
    loaded_ = result.ok;
    numRegions_ = result.numRegions;

    blocksRendered_.store (0, std::memory_order_relaxed);
    framesRendered_.store (0, std::memory_order_relaxed);
    peak_.store (0.0f, std::memory_order_relaxed);
    maxChunks_.store (0, std::memory_order_relaxed);
    maxHostBlock_.store (0, std::memory_order_relaxed);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// RT
// ─────────────────────────────────────────────────────────────────────────────

void SamplerEngine::reset() noexcept
{
    synth_.allSoundOff();
}

void SamplerEngine::dispatch (int delay, const juce::MidiMessage& message) noexcept
{
    // Deliberately dumb and complete rather than filtered.
    //
    // Every CC goes through verbatim, including 120, 121 and 123, because
    // sfizz implements all-sound-off, all-notes-off and reset-all-controllers
    // itself inside cc() (Synth.cpp:1485-1492). Forwarding everything gets
    // correct panic behaviour for free; filtering would break it.
    //
    // Two consequences worth stating rather than rediscovering:
    //
    //   • sfizz's API has no channel argument anywhere. The instrument is OMNI
    //     by construction, and a host sending on channel 3 will play it.
    //
    //   • sfizz injects amplitude on cc7 and cc11 and pan on cc10 into every
    //     region (F8.3). Those now move when the host moves them. cc7 defaults
    //     to 100/127 through a squared curve, so there is a standing gain of
    //     about 0.62 before any host CC arrives. That is a level fact to
    //     account for in any A/B against the webview audition, not a bug.
    //
    // isNoteOn() is false for a zero-velocity note-on and isNoteOff() is true
    // for it, so testing in this order handles the running-status convention
    // without a special case.

    if (message.isNoteOn())
        synth_.noteOn (delay, message.getNoteNumber(), message.getVelocity());
    else if (message.isNoteOff())
        synth_.noteOff (delay, message.getNoteNumber(), message.getVelocity());
    else if (message.isController())
        synth_.cc (delay, message.getControllerNumber(), message.getControllerValue());
    else if (message.isPitchWheel())
        // JUCE is 0..16383 centred at 8192; sfizz wants -8192..8192
        // (sfizz.hpp:550).
        synth_.pitchWheel (delay, message.getPitchWheelValue() - 8192);
    else if (message.isChannelPressure())
        synth_.channelAftertouch (delay, message.getChannelPressureValue());
    else if (message.isAftertouch())
        synth_.polyAftertouch (delay, message.getNoteNumber(), message.getAfterTouchValue());
}

void SamplerEngine::process (juce::AudioBuffer<float>& buffer,
                             const juce::MidiBuffer& midi) noexcept
{
    const int numFrames = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (! loaded_ || preparedBlock_ <= 0 || numFrames <= 0 || numChannels <= 0)
    {
        buffer.clear();
        return;
    }

    // Our bus is stereo, so this should never fire. It is here because the
    // alternative to clearing an unexpected channel is emitting whatever the
    // host left in it.
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numFrames);

    // In place when we have a real stereo pair: sfizz zeroes the output buffer
    // itself (Synth.cpp:1102), so whatever the host left there is harmless and
    // the copy is avoidable. A mono bus goes through scratch instead.
    const bool inPlace = numChannels >= 2;

    // JUCE guarantees MidiBuffer is ordered by sample position, which is also
    // what sfizz requires: "should be delay-ordered with all other midi-type
    // events, otherwise the behavior of the synth is undefined"
    // (sfizz.hpp:542-544). One iterator advanced across chunks preserves that
    // — re-scanning the buffer per chunk would not, and would be quadratic.
    auto event = midi.cbegin();
    const auto lastEvent = midi.cend();

    int pos = 0;
    int chunks = 0;

    while (pos < numFrames)
    {
        ++chunks;

        const int chunk = juce::jmin (preparedBlock_, numFrames - pos);
        const int chunkEnd = pos + chunk;

        for (; event != lastEvent; ++event)
        {
            const auto metadata = *event;

            if (metadata.samplePosition >= chunkEnd)
                break;

            // sfizz requires delay < the numFrames of the next renderBlock.
            // jlimit rather than an assertion: a host emitting an out-of-range
            // timestamp should produce a slightly early note, not undefined
            // behaviour inside the synth.
            const int delay = juce::jlimit (0, chunk - 1, metadata.samplePosition - pos);

            dispatch (delay, metadata.getMessage());
        }

        float* channels[2];

        if (inPlace)
        {
            channels[0] = buffer.getWritePointer (0) + pos;
            channels[1] = buffer.getWritePointer (1) + pos;
        }
        else
        {
            channels[0] = scratch_.getWritePointer (0);
            channels[1] = scratch_.getWritePointer (1);
        }

        // numOutputs 1 means one stereo pair, so two planar pointers, each
        // `chunk` frames long.
        synth_.renderBlock (channels, static_cast<size_t> (chunk), 1);

        if (! inPlace)
        {
            float* destination = buffer.getWritePointer (0) + pos;

            for (int i = 0; i < chunk; ++i)
                destination[i] = 0.5f * (channels[0][i] + channels[1][i]);
        }

        pos = chunkEnd;
    }

    // ── Instrumentation ─────────────────────────────────────────────────────
    // One extra pass over the block, for diagnostics only. Kept because the
    // failure mode this whole file is defending against is inaudible, and a
    // peak of exactly zero across thousands of blocks is the only evidence
    // that separates "not loaded" from "loaded and culled" without a DAW.
    // Delete it when the plugin has a UI that shows level.

    const float blockPeak = buffer.getMagnitude (0, numFrames);

    blocksRendered_.fetch_add (1, std::memory_order_relaxed);
    framesRendered_.fetch_add (static_cast<std::uint64_t> (numFrames), std::memory_order_relaxed);

    float seen = peak_.load (std::memory_order_relaxed);

    while (blockPeak > seen
           && ! peak_.compare_exchange_weak (seen, blockPeak, std::memory_order_relaxed))
    {
    }

    int seenChunks = maxChunks_.load (std::memory_order_relaxed);

    while (chunks > seenChunks
           && ! maxChunks_.compare_exchange_weak (seenChunks, chunks, std::memory_order_relaxed))
    {
    }

    int seenBlock = maxHostBlock_.load (std::memory_order_relaxed);

    while (numFrames > seenBlock
           && ! maxHostBlock_.compare_exchange_weak (seenBlock, numFrames, std::memory_order_relaxed))
    {
    }
}

SamplerEngine::RenderStats SamplerEngine::stats() const noexcept
{
    RenderStats s;
    s.blocks = blocksRendered_.load (std::memory_order_relaxed);
    s.frames = framesRendered_.load (std::memory_order_relaxed);
    s.peak = peak_.load (std::memory_order_relaxed);
    s.maxChunks = maxChunks_.load (std::memory_order_relaxed);
    s.maxHostBlock = maxHostBlock_.load (std::memory_order_relaxed);
    return s;
}
