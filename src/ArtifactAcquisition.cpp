// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
#include "ArtifactAcquisition.h"

// For fileNameFor only. This file does NOT resolve the outbox location itself --
// the directory is injected by the caller, so the naming rule and the path rule
// stay in one place each.
#include "RenderOutbox.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace kromosynth::acquisition
{

namespace
{

constexpr const char* kHttpsFallbackPrefix = "/__synthis_plugin/";
constexpr const char* kTicketHeader = "X-Extraction-Ticket";
constexpr const char* kStagingDirName = ".incoming";
constexpr const char* kSidecarName = "synthis.json";
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
    if (! url.startsWithIgnoreCase ("https://"))
        return false;

    const auto path = withoutQuery (url);

    return path.endsWithIgnoreCase (juce::String (kHttpsFallbackPrefix) + kSentinelAcquireHost)
        || path.endsWithIgnoreCase (juce::String (kHttpsFallbackPrefix) + kSentinelRenderHost);
}

/** Which verb a sentinel names, in either of its two spellings.

    ONE function that knows both, deliberately. A verb recognised as a custom
    scheme host but not as an https path segment would work everywhere it was
    tested and fail only on the fallback the fallback exists for.
*/
std::optional<Verb> verbOf (const juce::String& url)
{
    juce::String token;

    if (url.startsWithIgnoreCase (juce::String (kSentinelScheme) + "://"))
    {
        const auto afterScheme = url.fromFirstOccurrenceOf ("://", false, false);
        token = withoutQuery (afterScheme).upToFirstOccurrenceOf ("/", false, false);
    }
    else if (looksLikeHttpsFallback (url))
    {
        token = withoutQuery (url).fromLastOccurrenceOf ("/", false, false);
    }

    if (token.equalsIgnoreCase (kSentinelAcquireHost))
        return Verb::acquire;

    if (token.equalsIgnoreCase (kSentinelRenderHost))
        return Verb::render;

    return std::nullopt;
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

/** An archive entry name with '/' separators, whatever the writer used. */
juce::String normalisedName (juce::ZipFile& zip, int index)
{
    return zip.getEntry (index)->filename.replaceCharacter ('\\', '/');
}

/** The last path component. */
juce::String leafOf (const juce::String& name)
{
    const int slash = name.lastIndexOfChar ('/');
    return slash < 0 ? name : name.substring (slash + 1);
}

/** The directory part, with a trailing slash, and how deep it is.

    NOT upToLastOccurrenceOf: it returns the WHOLE string when the separator is
    absent (juce_String.cpp:1629), so a root-level "instrument.sfz" yielded the
    prefix "instrument.sfz" rather than "", and the root-level archive shape --
    the common one -- failed to install while the nested one worked. Found by
    the probe selftest.
*/
juce::String prefixOf (const juce::String& name, int& depth)
{
    const int slash = name.lastIndexOfChar ('/');

    depth = slash < 0
          ? 0
          : juce::StringArray::fromTokens (name.substring (0, slash), "/", "").size();

    // "instrument.sfz" -> ""; "sfz/instrument.sfz" -> "sfz/"
    return slash < 0 ? juce::String() : name.substring (0, slash + 1);
}

/** True when `name` lies under any of the given directory prefixes.

    An empty prefix is ignored rather than matching everything. It means "the
    archive root", and treating it as a directory to exclude would exclude the
    whole archive.
*/
bool isInsideAny (const juce::String& name, const juce::StringArray& prefixes)
{
    for (const auto& prefix : prefixes)
        if (prefix.isNotEmpty() && name.startsWith (prefix))
            return true;

    return false;
}

struct ArchiveRoot
{
    juce::String prefix;
    InstrumentLibrary::Kind kind = InstrumentLibrary::Kind::none;
    int depth = std::numeric_limits<int>::max();

    /** Prefixes of the instrument roots that were NOT chosen.

        Needed because an empty winning prefix means "the instrument is at the
        archive root", and the extraction filter then passes every entry. A
        sibling directory holding the other kind would come along with it --
        silently, and only as wasted disk, but it makes "only the chosen
        subtree" untrue. Carried out of the scan rather than recomputed,
        because the scan is the only place that knows what a root looks like.
    */
    juce::StringArray losers;
};

/** Finds the directory inside the archive that holds an instrument, and which
    kind of instrument it is.

    Two kinds, per ARTEFACT_CONTRACT §1: an `instrument.sfz` for the sampler, or
    an `instrument.wasm` WITH an `instrument.json` beside it for Faust. The pair
    is required, not preferred -- a .wasm alone is not a degraded instrument but
    an unplayable one, because the struct size and parameter byte offsets live
    in the JSON and cannot be recovered from the module. Same rule as
    InstrumentLibrary::resolve, deliberately.

    Shallowest wins, so the archive files may sit at the root or one level down
    under a format directory without a second convention. A tie goes to the
    sampler, matching the order resolve() tests in: if the two disagreed, an
    archive carrying both kinds would install one and load the other, which is
    the §O3 listing/loading split from the other side.

    Returns nullopt when the archive holds neither -- the non-sfz-primary case,
    which this plugin cannot use.
*/
std::optional<ArchiveRoot> findInstrumentRoot (juce::ZipFile& zip)
{
    // Collected first, because the .json may appear after the .wasm in entry
    // order and a single pass would judge the pair on incomplete information.
    juce::StringArray faustMetadataPrefixes;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = normalisedName (zip, i);

        if (leafOf (name).equalsIgnoreCase ("instrument.json"))
        {
            int depth = 0;
            faustMetadataPrefixes.add (prefixOf (name, depth));
        }
    }

    std::vector<ArchiveRoot> candidates;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = normalisedName (zip, i);

        // The whole leaf, not a suffix. endsWithIgnoreCase ("instrument.sfz")
        // also matches "my-instrument.sfz", which would pick a prefix from a
        // file the engine will never open.
        const auto leaf = leafOf (name);

        int depth = 0;
        const auto prefix = prefixOf (name, depth);

        auto kind = InstrumentLibrary::Kind::none;

        if (leaf.equalsIgnoreCase ("instrument.sfz"))
            kind = InstrumentLibrary::Kind::sampler;
        else if (leaf.equalsIgnoreCase ("instrument.wasm")
                 && faustMetadataPrefixes.contains (prefix))
            kind = InstrumentLibrary::Kind::faust;

        if (kind != InstrumentLibrary::Kind::none)
            candidates.push_back ({ prefix, kind, depth, {} });
    }

    ArchiveRoot best;

    for (const auto& candidate : candidates)
        if (candidate.depth < best.depth
            || (candidate.depth == best.depth
                && candidate.kind == InstrumentLibrary::Kind::sampler))
            best = candidate;

    if (best.kind == InstrumentLibrary::Kind::none)
        return std::nullopt;

    // A prefix equal to the winner's is the same directory, not a rival: one
    // folder can legitimately hold both an SFZ and a Faust pair, and pruning
    // it would delete the instrument that was just chosen.
    for (const auto& candidate : candidates)
        if (candidate.prefix.isNotEmpty() && candidate.prefix != best.prefix)
            best.losers.addIfNotAlreadyThere (candidate.prefix);

    return best;
}

/** Writes the display name into the sidecar WITHOUT discarding what is already
    there.

    This used to be a plain overwrite of a three-key object, which was correct
    while the plugin was the only writer. ARTEFACT_CONTRACT §2 makes the server
    the first writer -- provenance, dates, characteristics and the measured
    block all arrive in this file -- so an overwrite here would throw away
    everything the artefact came with and leave a library that can only sort by
    name.

    A malformed existing sidecar is replaced rather than merged into. There is
    nothing to merge with, and the name is worth more than an unparseable file.
*/
void stampSidecar (const juce::File& folder,
                   const juce::String& artifactId,
                   const juce::String& displayName)
{
    const auto sidecar = folder.getChildFile (kSidecarName);
    const bool hadOne = sidecar.existsAsFile();

    // Nothing to say and nothing to say it into. Writing an empty sidecar
    // would give every hand-placed instrument one.
    if (displayName.isEmpty() && ! hadOne)
        return;

    juce::var doc;

    if (hadOne)
        doc = juce::JSON::parse (sidecar.loadFileAsString());

    if (doc.getDynamicObject() == nullptr)
        doc = juce::var (new juce::DynamicObject());

    auto* obj = doc.getDynamicObject();

    // Only when there is one. An empty name must not overwrite a name the
    // server already put in the artefact.
    if (displayName.isNotEmpty())
        obj->setProperty ("name", displayName);

    obj->setProperty ("artifactId", artifactId);
    obj->setProperty ("acquiredAt", juce::Time::getCurrentTime().toISO8601 (true));

    sidecar.replaceWithText (juce::JSON::toString (doc));
}

/** Cleans a label for display.

    Not a path component, so the strict isSafeComponent rules would be wrong
    here -- a name may legitimately contain spaces and punctuation. What it must
    not contain is control characters, which would corrupt the sidecar and the
    list, or unbounded length, which would push everything else off the row.

    An empty result is fine: the caller falls back to the existing behaviour.
*/
juce::String sanitiseDisplayName (const juce::String& raw)
{
    juce::String cleaned;
    cleaned.preallocateBytes (128);

    for (auto c : raw)
    {
        if (c >= 32 && c != 127)
            cleaned << juce::String::charToString (c);
    }

    return cleaned.trim().substring (0, 96);
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
void tidyStaging (const juce::File& parentDir)
{
    // parentDir, not libraryRoot: the render outbox uses the same .incoming
    // convention and the same guard, so this took the more general name when
    // the second caller arrived rather than keeping one that would be wrong
    // half the time.
    const auto staging = parentDir.getChildFile (kStagingDirName);

    if (! staging.isDirectory())
        return;

    // ignoreHiddenFiles matters and is not cosmetic. JUCE counts hidden files by
    // default, and Finder drops a .DS_Store into any directory it looks at --
    // including this one. Without the flag the emptiness test is permanently
    // false on a real Mac, and the staging directory is never removed, while a
    // scratch directory under /tmp never acquires a .DS_Store and the selftest
    // passes. The test did not share the failure mode with the thing it stood
    // in for.
    //
    // deleteRecursively then removes the .DS_Store along with the directory,
    // which is right: it is Finder's litter inside our staging area, not
    // anybody's data.
    const auto realChildren = staging.getNumberOfChildFiles (
        juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles);

    if (realChildren == 0)
        staging.deleteRecursively();
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
    explicit StagingTidier (juce::File dir) : parentDir (std::move (dir)) {}
    ~StagingTidier() { tidyStaging (parentDir); }

    juce::File parentDir;

    JUCE_DECLARE_NON_COPYABLE (StagingTidier)
};

/** GETs one ticketed artefact file into `destination`.

    THE WHOLE NETWORK CONTRACT, in one copy: the route shape, the ticket header,
    the three status codes worth naming, and the Content-Length truncation
    check. Both verbs need every part of it. A second copy for the render path
    would have been four fewer lines to read today and one silent divergence
    later -- the truncation check is exactly the sort of thing that gets fixed
    in one copy and not the other.

    Writes to `destination` as given. The caller decides whether that is a
    staging path or a final one; this function does not create directories and
    does not clean up after itself except on a failed write, where leaving a
    partial file behind would be worse than the failure.

    Result::installedId is unused here. The caller fills it in with whatever it
    ended up producing.
*/
Result download (const Request& request,
                 const juce::String& baseUrl,
                 const juce::File& destination,
                 const std::function<void (double)>& onProgress)
{
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
        juce::FileOutputStream out (destination);

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
                discard (destination);
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
            discard (destination);
            return { false, {},
                     "Download was truncated: expected " + juce::String (total)
                         + " bytes, got " + juce::String (written) };
        }
    }

    return { true, {}, {} };
}

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

    // TWO verbs, acquire and render. An unknown one is not a malformed acquire,
    // and answering nullopt here keeps the caller blocking it -- which is what
    // stops a ticket reaching the network. Note this also catches the AUTH
    // sentinel, which shares the scheme; pageAboutToLoad handles that one first
    // so it never gets here, and if it ever did, blocked-and-unparsed is the
    // right outcome rather than a wrong one.
    const auto verb = verbOf (url);

    if (! verb.has_value())
        return std::nullopt;

    const juce::URL parsed (url);

    Request r;
    r.verb       = *verb;
    r.artifactId = param (parsed, "artifact");
    r.fileName   = param (parsed, "file");
    r.ticket     = param (parsed, "ticket");
    r.name       = sanitiseDisplayName (param (parsed, "name"));

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
    const auto name = sanitiseDisplayName (param (parsed, "name"));

    // Never interpolate the ticket, not even a prefix of it. A truncated
    // credential in a log is still a credential in a log, and "just the first
    // few characters" is how that argument always starts.
    //
    // The name IS logged. It is not a credential, and its absence was the whole
    // question when an acquired instrument turned up labelled with a ULID: the
    // log could not distinguish "the site sent no name" from "the plugin
    // dropped it", which took a diff of the deployed bundle to settle.
    return withoutQuery (url)
         + " artifact=" + (artifact.isNotEmpty() ? artifact : juce::String ("?"))
         + " name=" + (name.isNotEmpty() ? name.quoted() : juce::String ("absent"))
         + " ticket=" + (hasTicket ? "present" : "absent");
}

Result installFromArchive (const juce::File& archive,
                           const juce::String& artifactId,
                           const juce::File& libraryRoot,
                           const juce::String& displayName)
{
    if (! isSafeComponent (artifactId))
        return { false, {}, "Refusing an unsafe artefact id" };

    if (! archive.existsAsFile())
        return { false, {}, "Downloaded archive is missing" };

    juce::ZipFile zip (archive);

    if (zip.getNumEntries() == 0)
        return { false, {}, "Archive is empty or not a ZIP" };

    const auto root = findInstrumentRoot (zip);

    if (! root.has_value())
    {
        // The non-sfz-primary case, and now also the incomplete-Faust case.
        // Said plainly, because "installed but invisible" is the outcome if
        // this is allowed through: scan() skips a directory that resolves to
        // nothing and the user sees the download succeed and no instrument
        // appear.
        return { false, {},
                 "This artefact holds nothing the plugin can play. It needs an "
                 "instrument.sfz for the sampler, or an instrument.wasm with its "
                 "instrument.json for a Faust instrument." };
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
        const auto name = normalisedName (zip, i);

        if (root->prefix.isNotEmpty() && ! name.startsWith (root->prefix))
            continue;

        // The filter above is not enough when the winner is at the archive
        // root: an empty prefix passes every entry, so a sibling directory
        // holding the other kind would be extracted too. Found by the probe
        // selftest, on a shape the VI service does not currently emit -- it
        // always nests when it packs more than one format -- but the contract
        // allows a root primary beside a nested one, and this is the difference
        // between installing an instrument and installing an instrument plus a
        // whole unused sample set.
        if (isInsideAny (name, root->losers))
            continue;

        const auto result = zip.uncompressEntry (i, work);

        if (result.failed())
        {
            discard (work);
            return { false, {}, "Could not unpack the archive: " + result.getErrorMessage() };
        }
    }

    const auto unpacked = root->prefix.isEmpty() ? work : work.getChildFile (root->prefix);

    // The archive listing said these were there. If they are not on disk now,
    // the extraction quietly did not do what it reported.
    //
    // Both files for Faust, for the same reason findInstrumentRoot requires
    // both: a .wasm whose .json failed to extract installs an instrument that
    // appears in the list and cannot be loaded.
    const bool landed = root->kind == InstrumentLibrary::Kind::sampler
                      ? unpacked.getChildFile ("instrument.sfz").existsAsFile()
                      : unpacked.getChildFile ("instrument.wasm").existsAsFile()
                            && unpacked.getChildFile ("instrument.json").existsAsFile();

    if (! landed)
    {
        discard (work);
        return { false, {},
                 "Unpacked archive is missing the instrument files its listing promised" };
    }

    const auto destination = libraryRoot.getChildFile (artifactId);
    const auto replaced = staging.getChildFile (artifactId + ".replaced");

    // Written into the staging copy, so it arrives with the atomic move rather
    // than as a second step afterwards. A directory that appeared without its
    // sidecar would show the old ULID until the next download rewrote it.
    stampSidecar (unpacked, artifactId, displayName);

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
    // Symmetric with fetchRender's guard, and first for the same reason: a
    // request that arrives at the wrong path is a dispatch bug, and the two
    // wrong outcomes are a WAV handed to a ZIP unpacker or a ZIP dropped in the
    // outbox as though it were a sound. Refused before anything is created, so
    // a mislabelled request leaves no directory behind either.
    if (request.verb != Verb::acquire)
        return { false, {}, "Refusing a request that is not an acquisition" };

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

    if (auto fetched = download (request, baseUrl, archive, onProgress); ! fetched.ok)
        return fetched;

    auto result = installFromArchive (archive, request.artifactId, libraryRoot,
                                      request.name);

    discard (archive);

    return result;
}

Result fetchRender (const Request& request,
                    const juce::String& baseUrl,
                    const juce::File& outboxDir,
                    std::function<void (double)> onProgress)
{
    // Refused rather than coerced. Arriving here with the wrong verb means a
    // caller dispatched on something other than request.verb, and a WAV written
    // into the outbox by the acquire path -- or a ZIP by this one -- would be a
    // file that looks delivered and plays as noise.
    if (request.verb != Verb::render)
        return { false, {}, "Refusing a request that is not a render" };

    if (! request.isValid())
        return { false, {}, "Incomplete render request" };

    if (! isSafeComponent (request.artifactId) || ! isSafeComponent (request.fileName))
        return { false, {}, "Refusing an unsafe artefact id or file name" };

    if (! outboxDir.createDirectory())
        return { false, {}, "Could not create the renders directory" };

    const auto staging = outboxDir.getChildFile (kStagingDirName);

    if (! staging.createDirectory())
        return { false, {}, "Could not create a staging directory" };

    const StagingTidier tidier (outboxDir);

    // Staged and then renamed, for the same reason the instrument path stages:
    // the outbox is drained by the OTHER process, which must never see a file
    // that is still being written. The staging directory is hidden and the
    // drain skips hidden entries, so a half-downloaded render cannot be
    // collected as a finished one. Keyed on the artefact id rather than the
    // final name so two renders sharing a label cannot collide mid-flight.
    const auto partial = staging.getChildFile (request.artifactId + ".part");

    discard (partial);

    if (auto fetched = download (request, baseUrl, partial, onProgress); ! fetched.ok)
        return fetched;

    const auto name = kromosynth::renders::fileNameFor (request.name, request.fileName);
    const auto destination = outboxDir.getChildFile (name);

    // Overwrite. fileNameFor makes one render produce one name, so a collision
    // here means this same render is being delivered again -- see the note
    // there on why that should replace rather than accumulate.
    discard (destination);

    if (! partial.moveFileTo (destination))
    {
        discard (partial);
        return { false, {}, "Could not move the render into the outbox" };
    }

    // The FILE NAME, not the artefact id. This is what the shell shows and what
    // the user will look for in Files.app; the id is meaningless to both.
    return { true, name, "Saved " + name };
}

} // namespace kromosynth::acquisition
