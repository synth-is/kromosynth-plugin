// ─────────────────────────────────────────────────────────────────────────────
// DISPOSABLE. Rewrite or delete without ceremony.
//
// Stage 0 processor, now with audio. No parameters, no state, no UI bridge.
//
// Everything here that looks like a decision is still a placeholder:
//   • getStateInformation writes nothing. State is Stage 1.
//   • There is no AudioProcessorValueTreeState, on purpose. Adding one now
//     would be an abstraction over zero parameters.
//   • The SFZ path is compiled in. Changing instrument is a reconfigure, not
//     a code edit and not a UI action — see KROMOSYNTH_SFZ_PATH.
//
// Do not build on this file. Build beside it and delete it.
//
// One thing here is NOT disposable: the OFF-thread barrier around sfizz. The
// engine does not provide it (see SamplerEngine.h) and should not, because the
// barrier is a JUCE concept. Whatever replaces this file has to keep it.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#if KROMOSYNTH_HAS_SAMPLER
class SamplerEngine;
#endif

#include "InstrumentLibrary.h"
#include "AcquisitionRunner.h"

class KromosynthAudioProcessor : public juce::AudioProcessor
{
public:
    KromosynthAudioProcessor();

    // Defined in the .cpp, not defaulted here. engine_ is a unique_ptr to an
    // incomplete type at this point, and ~unique_ptr needs the definition.
    ~KromosynthAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Synth.is"; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // Read from sfizz at LOAD time and cached, so this message-thread query
    // never touches the introspection API. See SamplerEngine::tailSeconds().
    //
    // Returns the WORST case, which for both artefacts on disk is 8.3 s
    // (ampeg_release=0.3 plus ampeg_release_oncc73=8). The typical case at
    // default CC is 0.3 s, measured: a Reaper render is at exactly 0.0 from
    // one second after the last note-off. Reporting the worst case makes a
    // host render silence for the difference, which is the cheap direction to
    // be wrong in; the other direction truncates a real tail whenever CC73 is
    // up.
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Stage 0 instrumentation. The editor appends host-observable lifecycle
    // events here; see PluginEditor.cpp for where the log lands on disk.
    // Lives on the processor rather than the editor because the whole point
    // is to survive the editor being destroyed and recreated.
    void logEvent (const juce::String& event);

    // Exposed so async callbacks can capture the File BY VALUE and still log
    // after this processor is gone. juce::File is a value type holding a path
    // string — appending to it touches no live object, which is exactly what
    // the probe's editor-destroyed path needs.
    const juce::File& getLogFile() const noexcept { return logFile_; }

   #if KROMOSYNTH_HAS_SAMPLER
    /** What a load attempt did. `ok` and `numRegions` are separate on purpose:
        F11 established that loadSfzFile returns true for an instrument whose
        every region was culled, so "it loaded" and "there is an instrument"
        are different claims and the UI must be able to tell them apart.
    */
    struct LoadOutcome
    {
        bool ok = false;
        int numRegions = 0;
        double tailSeconds = 0.0;
        juce::String id;
        juce::String detail;
    };

    /** Loads an instrument from the local library by id.

        Called on the MESSAGE thread, from a native function invoked by the
        shell's JavaScript. sfizz's loadSfzFile is OFF-annotated and that does
        not change because the caller is a webview, so this takes the same
        suspendProcessing barrier prepareToPlay does.
    */
    LoadOutcome loadInstrument (const juce::String& id);

    /** What is loaded now. An empty id means the compiled-in default. */
    LoadOutcome currentInstrument() const { return current_; }

    const InstrumentLibrary& library() const noexcept { return library_; }
   #endif

    /** Downloads in flight and their outcomes.

        Lives here rather than on the editor so a download survives the editor
        being destroyed, which hosts do on every window close. See
        AcquisitionRunner.h.

        Outside the sampler guard on purpose. Acquiring an instrument is an HTTP
        fetch and a ZIP unpack into ~/Music; none of it involves sfizz. Guarding
        it would make downloads depend on an audio engine they never call --
        accidental coupling that stays invisible until someone builds without
        the sampler.
    */
    AcquisitionRunner& acquisitions() noexcept { return acquisitions_; }

private:
   #if KROMOSYNTH_HAS_SAMPLER
    // The compiled-in SFZ path, resolved in one place so the empty-path and
    // missing-file cases are answered once.
    juce::File sfzFile() const;

    void logRenderStats (const char* reason);

    // Created lazily on the first prepareToPlay, NOT in the constructor.
    //
    // Hosts instantiate during a scan and time out while doing it — Debug
    // measured 1532 ms under auval before there was any audio at all.
    // Deferring construction keeps the scan footprint exactly what Task 1
    // measured and makes every millisecond sfizz costs attributable to
    // activation instead.
    //
    // Mutated only inside the suspend window. The read on the audio thread is
    // not a data race: suspendProcessing and processBlock take the same
    // CriticalSection, and that supplies the happens-before edge. See
    // SamplerEngine.h.
    std::unique_ptr<SamplerEngine> engine_;

    InstrumentLibrary library_;

    // Message-thread only.
    LoadOutcome current_;
   #endif

    AcquisitionRunner acquisitions_;

    juce::File logFile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KromosynthAudioProcessor)
};
