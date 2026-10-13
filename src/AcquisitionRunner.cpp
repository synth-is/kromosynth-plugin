// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
#include "AcquisitionRunner.h"

AcquisitionRunner::~AcquisitionRunner()
{
    // Bounded rather than indefinite. A download stuck on a dead socket must
    // not hold up the destruction of the processor -- that would present to the
    // user as the host hanging on plugin removal, which is a far worse failure
    // than a lost download. The connection timeout in fetchAndInstall is 20s,
    // so 25s here means a normal in-flight job finishes and a wedged one does
    // not become a hang.
    pool_.removeAllJobs (true, 25000);
}

bool AcquisitionRunner::start (const kromosynth::acquisition::Request& request,
                               const juce::String& baseUrl,
                               const juce::File& libraryRoot,
                               const juce::File& rendersOutbox)
{
    {
        const juce::ScopedLock sl (lock_);

        for (const auto& s : statuses_)
            if (s.artifactId == request.artifactId && ! s.finished)
                return false;

        Status fresh;
        fresh.artifactId = request.artifactId;
        fresh.verb = request.verb;
        fresh.message = "Starting";
        statuses_.push_back (fresh);
    }

    // Captured by value. The job outlives this call by definition, and a
    // reference to the caller's Request would be a dangling read the moment the
    // navigation handler returns.
    pool_.addJob ([this, request, baseUrl, libraryRoot, rendersOutbox]
    {
        update (request.artifactId, [] (Status& s) { s.message = "Downloading"; });

        const auto onProgress = [this, id = request.artifactId] (double fraction)
        {
            update (id, [fraction] (Status& s) { s.progress = fraction; });
        };

        // Dispatch on the VERB, which came off the sentinel, and not on
        // anything inferred from the file name here. The site said which of the
        // two this is; guessing from ".wav" would be a second opinion able to
        // disagree with the first.
        const auto result = request.verb == kromosynth::acquisition::Verb::render
            ? kromosynth::acquisition::fetchRender (request, baseUrl, rendersOutbox, onProgress)
            : kromosynth::acquisition::fetchAndInstall (request, baseUrl, libraryRoot, onProgress);

        update (request.artifactId, [&result] (Status& s)
        {
            s.finished = true;
            s.ok = result.ok;
            s.message = result.message;
            s.progress = result.ok ? 1.0 : s.progress;
        });

        // Copied under the lock and invoked outside it: the handler loads an
        // instrument, which suspends processing, and holding this lock across
        // that would put a download thread's lock in the path of the audio
        // thread's barrier.
        std::function<void (juce::String, bool, kromosynth::acquisition::Verb)> handler;
        {
            const juce::ScopedLock sl (lock_);
            handler = onFinished_;
        }

        if (handler != nullptr)
            juce::MessageManager::callAsync ([handler,
                                              id = request.artifactId,
                                              ok = result.ok,
                                              verb = request.verb] { handler (id, ok, verb); });
    });

    return true;
}

std::vector<AcquisitionRunner::Status> AcquisitionRunner::snapshot() const
{
    const juce::ScopedLock sl (lock_);
    return statuses_;
}

void AcquisitionRunner::clearFinished()
{
    const juce::ScopedLock sl (lock_);

    statuses_.erase (std::remove_if (statuses_.begin(), statuses_.end(),
                                     [] (const Status& s) { return s.finished; }),
                     statuses_.end());
}

void AcquisitionRunner::setCompletionHandler (
    std::function<void (juce::String, bool, kromosynth::acquisition::Verb)> handler)
{
    const juce::ScopedLock sl (lock_);
    onFinished_ = std::move (handler);
}

void AcquisitionRunner::update (const juce::String& artifactId,
                                std::function<void (Status&)> mutate)
{
    const juce::ScopedLock sl (lock_);

    for (auto& s : statuses_)
    {
        if (s.artifactId == artifactId)
        {
            mutate (s);
            return;
        }
    }
}
