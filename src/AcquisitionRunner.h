// AcquisitionRunner — downloads in flight, and what became of them.
//
// Stage 1, WP_SYNTHIS_PLUGIN SS12. KEEPABLE.
//
// ── Why the processor owns this and not the editor ───────────────────────────
//
// Two reasons, and the second is the one that removes a whole class of bug.
//
// A download must survive the editor. Hosts destroy and recreate editors freely
// -- every window close does it -- and a user who clicks download and then
// closes the plugin window should not silently lose the instrument.
//
// And there is then no callback to get wrong. If this notified the editor, the
// notification would arrive on a worker thread, for an editor that may have
// been destroyed since the job started, and would need a SafePointer and a
// callAsync to be safe. Instead nothing is pushed: the shell polls
// getAcquisitions, which is a native function that reads a snapshot under a
// lock. There is no lifetime relationship to get right because there is no
// relationship.
//
// ── Threading ────────────────────────────────────────────────────────────────
//
// One pooled thread. Downloads are serialised deliberately: they are network-
// bound, the user is realistically acquiring one instrument at a time, and a
// single worker means the staging directory cannot be contended.
//
// Nothing here touches the audio thread. fetchAndInstall does blocking network
// and disk I/O, which is exactly why it must not be called from the message
// thread either -- a blocked message thread is a frozen host UI.

#pragma once

#include "ArtifactAcquisition.h"

#include <juce_core/juce_core.h>
// For MessageManager: the completion handler is dispatched from the pool
// thread to the message thread, and juce_core alone does not have it.
#include <juce_events/juce_events.h>

#include <vector>

class AcquisitionRunner
{
public:
    struct Status
    {
        juce::String artifactId;
        juce::String message;
        double progress = 0.0;   // 0..1, or -1 when the size is unknown
        bool finished = false;
        bool ok = false;

        /** Which of the two jobs this is.

            Carried rather than inferred, so the shell can say "saved a render"
            where it means that and "added an instrument" where it means that.
            An earlier version of the finished line said "instrument(s)" for
            everything, which would have reported a WAV dropped in the outbox as
            an instrument added to a library it never reached.
        */
        kromosynth::acquisition::Verb verb = kromosynth::acquisition::Verb::acquire;
    };

    AcquisitionRunner() = default;
    ~AcquisitionRunner();

    /** Queues a download. Returns false if this artefact is already in flight,
        which is a real case: a double click on the site would otherwise start
        two downloads racing for the same staging path.

        BOTH destinations are passed, and the verb on the request decides which
        one is used. Resolving either of them in here instead would put a path
        rule in the one class that has no business knowing one -- and would make
        the runner untestable against a scratch directory, which is the reason
        fetchAndInstall took its root as a parameter in the first place.
    */
    bool start (const kromosynth::acquisition::Request& request,
                const juce::String& baseUrl,
                const juce::File& libraryRoot,
                const juce::File& rendersOutbox);

    /** A copy of the current state, safe to read from the message thread. */
    std::vector<Status> snapshot() const;

    /** Drops finished entries. Called by the shell once it has displayed them,
        so the list does not grow for the life of the session.
    */
    void clearFinished();

    /** Called on the MESSAGE THREAD when a job finishes, with its id, whether it
        succeeded, and which verb it was.

        Exists so the processor can make a freshly acquired instrument the
        active one. The shell cannot do that job: a download starts while the
        user is on the site, so the shell is hidden, and WKWebView throttles
        timers in a hidden view -- its poll typically wakes up to find the work
        long finished. Anything that must happen AT completion has to happen
        here.

        THE VERB IS PASSED because the handler's whole job is wrong for one of
        them: a render is a file in an outbox, not an instrument, and feeding
        its id to loadPendingInstrument would ask the library for a directory
        that does not exist. That would fail quietly today -- and quietly is
        exactly how it would still be there when something later depended on it.

        Marshalled to the message thread rather than called on the pool thread,
        because the only interesting thing to do from it is load an instrument,
        and that suspends processing and touches the editor.
    */
    void setCompletionHandler (
        std::function<void (juce::String, bool, kromosynth::acquisition::Verb)> handler);

private:
    void update (const juce::String& artifactId,
                 std::function<void (Status&)> mutate);

    std::function<void (juce::String, bool, kromosynth::acquisition::Verb)> onFinished_;

    juce::ThreadPool pool_ { 1 };
    mutable juce::CriticalSection lock_;
    std::vector<Status> statuses_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AcquisitionRunner)
};
