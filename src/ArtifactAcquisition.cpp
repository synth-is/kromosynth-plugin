#include "ArtifactAcquisition.h"

#include <juce_core/juce_core.h>

namespace kromosynth::acquisition
{

namespace
{

constexpr const char* kHttpsFallbackPath = "/__synthis_plugin/acquire";
constexpr const char* kTicketHeader = "X-Extraction-Ticket";
constexpr const char* kStagingDirName = ".incoming";
constexpr int kConnectionTimeoutMs = 20000;

/** Reads one query parameter. juce::URL parses these for us; doing it by hand
    would mean re-implementing percent-decoding, and the ticket is base64url so
    it contains '-' and '_' but never '%' -- which is exactly the kind of "works
    on the sample I tried" reasoning this project has been bitten by.
*/
juce::String param (const juce::URL& url, const juce::String& key)
{
    const auto index = url.getParameterNames().indexOf (key, true);
    return index >= 0 ? url.getParameterValues()[index] : juce::String();
}

/** Strips the query string, keeping enough to identify the request. */
juce::String withoutQuery (const juce::String& url)
{
    return url.upToFirstOccurrenceOf ("?", false, false);
}

bool looksLikeHttpsFallback (const juce::String& url)
{
    return url.startsWithIgnoreCase ("https://")
        && withoutQuery (url).endsWithIgnoreCase (kHttpsFallbackPath);
}

/** Rejects an id that could escape the library root, or name a hidden
    directory that the scan would then trip over.

    InstrumentLibrary::pathFor already guards ids arriving from JavaScript. This
    is a separate entry point for a value arriving from a URL, so it guards
    separately rather than trusting that the other one ran.
*/
bool isSafeComponent (const juce::String& s)
{
    if (s.isEmpty() || s.length() > 128)
        return false;

    if (s.containsChar ('/') || s.containsChar ('\\') || s.contains ("..")
        || s.startsWithChar ('.'))
        return false;

    for (auto c : s)
        if (! (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_' || c == '.'))
            return false;

    return true;
}

/** Finds the directory prefix inside the archive that contains instrument.sfz.

    Shallowest wins. A multi-format archive has exactly one sfz/ subtree, but
    if a future archive ever nested more deeply the shallowest is the one the
    generator considers primary.

    Returns nullopt when the archive has no instrument.sfz anywhere -- the
    non-sfz-primary case, which this plugin cannot use.
*/
std::optional<juce::String> findInstrumentPrefix (juce::ZipFile& zip)
{
    std::optional<juce::String> best;
    int bestDepth = std::numeric_limits<int>::max();

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry (i)->filename.replaceCharacter ('\\', '/');

        if (! name.endsWithIgnoreCase ("instrument.sfz"))
            continue;

        // NOT upToLastOccurrenceOf: it returns the WHOLE string when the
        // separator is absent (juce_String.cpp:1629), so a root-level
        // "instrument.sfz" yielded the prefix "instrument.sfz" rather than "",
        // and the root-level archive shape -- the common one -- failed to
        // install while the nested one worked. Found by the probe selftest.
        const int slash = name.lastIndexOfChar ('/');

        // "instrument.sfz" -> prefix ""; "sfz/instrument.sfz" -> prefix "sfz/"
        const juce::String prefix = slash < 0 ? juce::String() : name.substring (0, slash + 1);
        const int depth = slash < 0
                        ? 0
                        : juce::StringArray::fromTokens (name.substring (0, slash), "/", "").size();

        if (depth < bestDepth)
        {
            bestDepth = depth;
            best = prefix;
        }
    }

    return best;
}

/** Deletes a staging leftover without caring whether it was there. */
void discard (const juce::File& f)
{
    if (f.isDirectory())
        f.deleteRecursively();
    else if (f.existsAsFile())
        f.deleteFile();
}

/** Removes the staging directory once nothing is left in it.

    Called from BOTH entry points, and that is the point. It used to live only
    at the end of installFromArchive, where it looked correct and was: the
    selftest drives installFromArchive directly, staging is empty by then, and
    the check reported a clean library.

    On the real path it never fired. fetchAndInstall downloads the archive INTO
    staging, so when installFromArchive reached its cleanup the .zip was still
    sitting there, the directory was not empty, and the deletion was skipped --
    after which fetchAndInstall removed the archive and left an empty hidden
    directory in the user's instrument folder for good.

    Harmless in itself; scan() ignores a directory with no instrument.sfz. The
    part worth keeping is that a green check covered a path it did not run.
*/
void tidyStaging (const juce::File& libraryRoot)
{
    const auto staging = libraryRoot.getChildFile (kStagingDirName);

    if (staging.isDirectory()
        && staging.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
    {
        staging.deleteRecursively();
    }
}

/** Runs tidyStaging on the way out, however we leave.

    Calling tidyStaging at the two success returns was not enough, and the
    end-to-end run is what showed it: fetchAndInstall creates the staging
    directory early and then has eight early returns for network, HTTP, disk and
    ticket failures, none of which reached the cleanup. Three failed downloads
    in a row left an empty hidden directory in the user's instrument folder.

    A guard rather than another call at each return, because the next early
    return added by anyone would otherwise reintroduce it silently.
*/
struct StagingTidier
{
    explicit StagingTidier (juce::File root) : libraryRoot (std::move (root)) {}
    ~StagingTidier() { tidyStaging (libraryRoot); }

    juce::File libraryRoot;

    JUCE_DECLARE_NON_COPYABLE (StagingTidier)
};

} // namespace

bool isSentinel (const juce::String& url)
{
    return url.startsWithIgnoreCase (juce::String (kSentinelScheme) + "://")
        || looksLikeHttpsFallback (url);
}

std::optional<Request> parseSentinel (const juce::String& url)
{
    if (! isSentinel (url))
        return std::nullopt;

    if (url.startsWithIgnoreCase (juce::String (kSentinelScheme) + "://"))
    {
        // Only the acquire verb exists. An unknown verb is not a malformed
        // acquire, and answering nullopt keeps the caller blocking it.
        const auto afterScheme = url.fromFirstOccurrenceOf ("://", false, false);
        const auto host = withoutQuery (afterScheme).upToFirstOccurrenceOf ("/", false, false);

        if (! host.equalsIgnoreCase (kSentinelAcquireHost))
            return std::nullopt;
    }

    const juce::URL parsed (url);

    Request r;
    r.artifactId = param (parsed, "artifact");
    r.fileName   = param (parsed, "file");
    r.ticket     = param (parsed, "ticket");

    if (! r.isValid())
        return std::nullopt;

    if (! isSafeComponent (r.artifactId) || ! isSafeComponent (r.fileName))
        return std::nullopt;

    return r;
}

juce::String redactForLog (const juce::String& url)
{
    if (! isSentinel (url))
        return url;

    const juce::URL parsed (url);
    const auto artifact = param (parsed, "artifact");
    const auto hasTicket = param (parsed, "ticket").isNotEmpty();

    // Never interpolate the ticket, not even a prefix of it. A truncated
    // credential in a log is still a credential in a log, and "just the first
    // few characters" is how that argument always starts.
    return withoutQuery (url)
         + " artifact=" + (artifact.isNotEmpty() ? artifact : juce::String ("?"))
         + " ticket=" + (hasTicket ? "present" : "absent");
}

Result installFromArchive (const juce::File& archive,
                           const juce::String& artifactId,
                           const juce::File& libraryRoot)
{
    if (! isSafeComponent (artifactId))
        return { false, {}, "Refusing an unsafe artefact id" };

    if (! archive.existsAsFile())
        return { false, {}, "Downloaded archive is missing" };

    juce::ZipFile zip (archive);

    if (zip.getNumEntries() == 0)
        return { false, {}, "Archive is empty or not a ZIP" };

    const auto prefix = findInstrumentPrefix (zip);

    if (! prefix.has_value())
    {
        // The non-sfz-primary case. Said plainly, because "installed but
        // invisible" is the outcome if this is allowed through: scan() skips a
        // directory with no instrument.sfz and the user sees nothing.
        return { false, {},
                 "This artefact contains no instrument.sfz. The plugin's sampler "
                 "needs an SFZ; re-render the instrument in SFZ format." };
    }

    const auto staging = libraryRoot.getChildFile (kStagingDirName);
    const auto work = staging.getChildFile (artifactId + ".work");

    discard (work);

    if (! work.createDirectory())
        return { false, {}, "Could not create a staging directory" };

    // Only the chosen subtree. A multi-format archive carries a full sample set
    // per format, and extracting all of them to throw most away would multiply
    // the disk churn for no gain.
    //
    // uncompressEntry rather than a hand-rolled extractor: JUCE already rejects
    // entries that escape the target directory (juce_ZipFile.cpp:470) and
    // refuses to follow symlinks by default (:490). Re-implementing those
    // checks here would mean two places to get them right.
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry (i)->filename.replaceCharacter ('\\', '/');

        if (prefix->isNotEmpty() && ! name.startsWith (*prefix))
            continue;

        const auto result = zip.uncompressEntry (i, work);

        if (result.failed())
        {
            discard (work);
            return { false, {}, "Could not unpack the archive: " + result.getErrorMessage() };
        }
    }

    const auto unpacked = prefix->isEmpty() ? work : work.getChildFile (*prefix);

    if (! unpacked.getChildFile ("instrument.sfz").existsAsFile())
    {
        // The prefix scan said there was one. If it is not on disk now, the
        // extraction quietly did not do what it reported.
        discard (work);
        return { false, {}, "Unpacked archive has no instrument.sfz where one was expected" };
    }

    const auto destination = libraryRoot.getChildFile (artifactId);
    const auto replaced = staging.getChildFile (artifactId + ".replaced");

    discard (replaced);

    const bool hadPrevious = destination.exists();

    if (hadPrevious && ! destination.moveFileTo (replaced))
    {
        discard (work);
        return { false, {}, "An earlier copy is in the way and could not be moved aside" };
    }

    // The atomic step. Before this the instrument does not exist; after it, it
    // is complete. There is no window in which scan() sees a directory with
    // samples and no SFZ.
    if (! unpacked.moveFileTo (destination))
    {
        if (hadPrevious)
            replaced.moveFileTo (destination);

        discard (work);
        return { false, {}, "Could not move the instrument into the library" };
    }

    discard (replaced);
    discard (work);

    // Best effort. A leftover empty staging directory is harmless, and failing
    // an otherwise-successful install over it would be absurd.
    tidyStaging (libraryRoot);

    return { true, artifactId,
             hadPrevious ? "Replaced an existing instrument" : "Installed" };
}

Result fetchAndInstall (const Request& request,
                        const juce::String& baseUrl,
                        const juce::File& libraryRoot,
                        std::function<void (double)> onProgress)
{
    if (! request.isValid())
        return { false, {}, "Incomplete acquisition request" };

    if (! isSafeComponent (request.artifactId) || ! isSafeComponent (request.fileName))
        return { false, {}, "Refusing an unsafe artefact id or file name" };

    if (! libraryRoot.createDirectory())
        return { false, {}, "Could not create the instrument library directory" };

    const auto staging = libraryRoot.getChildFile (kStagingDirName);

    if (! staging.createDirectory())
        return { false, {}, "Could not create a staging directory" };

    const StagingTidier tidier (libraryRoot);

    const auto archive = staging.getChildFile (request.artifactId + ".zip");

    discard (archive);

    const auto endpoint = baseUrl.trimCharactersAtEnd ("/")
                        + "/vi/plugin-file/" + request.artifactId + "/" + request.fileName;

    int statusCode = 0;

    // The ticket goes in a header, never the query string. These routes sit
    // behind Cloudflare and a credential in a URL is a credential in somebody
    // else's log files.
    auto stream = juce::URL (endpoint).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders (juce::String (kTicketHeader) + ": " + request.ticket)
            .withConnectionTimeoutMs (kConnectionTimeoutMs)
            .withStatusCode (&statusCode));

    if (stream == nullptr)
        return { false, {}, "Could not reach the download service" };

    if (statusCode == 401)
        return { false, {}, "The download ticket was rejected or has expired. "
                            "Click download on the site again." };

    if (statusCode == 503)
        return { false, {}, "Ticketed downloads are not configured on the server" };

    if (statusCode < 200 || statusCode >= 300)
        return { false, {}, "Download failed with HTTP " + juce::String (statusCode) };

    const auto total = stream->getTotalLength();

    {
        juce::FileOutputStream out (archive);

        if (! out.openedOk())
            return { false, {}, "Could not open the staging file for writing" };

        juce::HeapBlock<char> buffer (65536);
        juce::int64 written = 0;

        for (;;)
        {
            const auto read = stream->read (buffer.getData(), 65536);

            if (read <= 0)
                break;

            if (! out.write (buffer.getData(), (size_t) read))
            {
                discard (archive);
                return { false, {}, "Ran out of disk space, or the write failed" };
            }

            written += read;

            if (onProgress)
                onProgress (total > 0 ? (double) written / (double) total : -1.0);
        }

        out.flush();

        // A truncated download that is never checked is the failure mode that
        // looks like a corrupt instrument three steps later. Content-Length is
        // set by the route deliberately (it has to be, or Cloudflare buffers),
        // so when it is present it can be trusted enough to compare against.
        if (total > 0 && written != total)
        {
            discard (archive);
            return { false, {},
                     "Download was truncated: expected " + juce::String (total)
                         + " bytes, got " + juce::String (written) };
        }
    }

    auto result = installFromArchive (archive, request.artifactId, libraryRoot);

    discard (archive);

    return result;
}

} // namespace kromosynth::acquisition
