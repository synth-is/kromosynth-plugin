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
                               const juce::File& libraryRoot)
{
    {
        const juce::ScopedLock sl (lock_);

        for (const auto& s : statuses_)
            if (s.artifactId == request.artifactId && ! s.finished)
                return false;

        Status fresh;
        fresh.artifactId = request.artifactId;
        fresh.message = "Starting";
        statuses_.push_back (fresh);
    }

    // Captured by value. The job outlives this call by definition, and a
    // reference to the caller's Request would be a dangling read the moment the
    // navigation handler returns.
    pool_.addJob ([this, request, baseUrl, libraryRoot]
    {
        update (request.artifactId, [] (Status& s) { s.message = "Downloading"; });

        const auto result = kromosynth::acquisition::fetchAndInstall (
            request, baseUrl, libraryRoot,
            [this, id = request.artifactId] (double fraction)
            {
                update (id, [fraction] (Status& s) { s.progress = fraction; });
            });

        update (request.artifactId, [&result] (Status& s)
        {
            s.finished = true;
            s.ok = result.ok;
            s.message = result.message;
            s.progress = result.ok ? 1.0 : s.progress;
        });
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
