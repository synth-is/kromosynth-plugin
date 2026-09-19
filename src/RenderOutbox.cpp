#include "RenderOutbox.h"

#include "InstrumentLibrary.h"

#include <cstring>

namespace kromosynth::renders
{

namespace
{

constexpr const char* kFolderName  = "Renders";
constexpr const char* kStagingName = ".incoming";

/** Newest first, for juce::Array::sort.

    The only one now. RenderGutter used to keep an identical copy because it
    scanned the directory itself; RenderList calls list() instead, so the
    ordering and the reading both live here.
*/
struct ByNewestFirst
{
    static int compareElements (const juce::File& a, const juce::File& b)
    {
        const auto ta = a.getLastModificationTime().toMilliseconds();
        const auto tb = b.getLastModificationTime().toMilliseconds();

        return ta == tb ? 0 : (ta > tb ? -1 : 1);
    }
};

/** Long enough to stay recognisable, short enough that a DAW's import list and
    Files.app both show the end of it. 80 is not a measured number; it is the
    same order as the 96 the instrument label uses, minus the extension.
*/
constexpr int kMaxStemLength = 80;

} // namespace

juce::File outbox()
{
    return InstrumentLibrary::containerRoot().getChildFile (kFolderName);
}

juce::File staging()
{
    return outbox().getChildFile (kStagingName);
}

juce::File deliveredRoot()
{
   #if JUCE_IOS
    // userDocumentsDirectory resolves per PROCESS. In the container app that is
    // the folder Files.app shows; in the appex it is a private one nobody sees.
    // drain() is the only caller and it is gated on the wrapper type by its
    // caller -- see the header.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile (kFolderName);
   #else
    return outbox();
   #endif
}

juce::String fileNameFor (const juce::String& displayName,
                          const juce::String& fallbackName)
{
    // createLegalFileName, not a hand-rolled character filter. The site's label
    // has already been stripped of control characters, but it can still carry
    // a '/' or a ':' -- neither of which is a problem in a UI row and both of
    // which are a problem in a path. JUCE's version also knows the reserved
    // Windows device names, which matters for the desktop builds sharing this.
    auto stem = juce::File::createLegalFileName (displayName).trim();

    // A label ending in ".wav" would otherwise become "Bell.wav.wav".
    if (stem.endsWithIgnoreCase (".wav"))
        stem = stem.dropLastCharacters (4).trim();

    // Trailing dots and spaces survive createLegalFileName and are a nuisance
    // on every platform that has ever had a filesystem.
    while (stem.isNotEmpty() && (stem.getLastCharacter() == '.' || stem.getLastCharacter() == ' '))
        stem = stem.dropLastCharacters (1);

    if (stem.length() > kMaxStemLength)
        stem = stem.substring (0, kMaxStemLength).trim();

    if (stem.isNotEmpty())
        return stem + ".wav";

    // The artefact's own name. Already safe -- the VI service builds it from an
    // id fragment and a pitch range -- but run through the same guard rather
    // than trusted, because "it is generated so it must be fine" is the shape
    // of assumption this file exists to avoid.
    auto fallback = juce::File::createLegalFileName (fallbackName).trim();

    if (fallback.isNotEmpty())
        return fallback.endsWithIgnoreCase (".wav") ? fallback : fallback + ".wav";

    return "render.wav";
}

DrainResult drain()
{
    DrainResult result;
   #if JUCE_IOS
    const auto from = outbox();

    if (! from.isDirectory())
        return result;   // nothing has ever been delivered here; not a failure

    const auto to = deliveredRoot();

    if (! to.createDirectory())
    {
        result.failed = 1;
        return result;
    }

    // findFiles, not findFilesAndDirectories: `.incoming` is a directory and is
    // excluded by that alone, which matters because a download may be in flight
    // in the OTHER process right now. Moving a partial file into Documents
    // would present a truncated WAV as a finished render, which is worse than a
    // late one.
    //
    // The isHidden() check below is a SECOND guard, and not the same one: JUCE
    // counts hidden files unless told not to, and Finder's .DS_Store is a file
    // rather than a directory, so findFiles alone would hand it over.
    juce::Array<juce::File> pending;
    from.findChildFiles (pending, juce::File::findFiles, false);

    for (const auto& f : pending)
    {
        if (f.isHidden())
            continue;

        auto destination = to.getChildFile (f.getFileName());

        // Overwrite rather than uniquify. fileNameFor() makes the same render
        // produce the same name, so a collision means this exact render is
        // being delivered again -- replacing it is what the user asked for, and
        // "Bell (2).wav" beside an identical "Bell.wav" is not.
        if (destination.exists())
            destination.deleteFile();

        if (f.moveFileTo (destination))
        {
            ++result.moved;

            if (result.firstName.isEmpty())
                result.firstName = destination.getFileName();
        }
        else
        {
            // Left in the outbox deliberately. The next launch tries again,
            // which is the right behaviour for a transient failure and harmless
            // for a permanent one.
            ++result.failed;
        }
    }
   #endif

    return result;
}

double wavSeconds (const juce::File& f)
{
    juce::FileInputStream in (f);

    if (! in.openedOk())
        return 0.0;

    // RIFF....WAVE
    char riff[4] = {};
    if (in.read (riff, 4) != 4 || std::memcmp (riff, "RIFF", 4) != 0)
        return 0.0;

    in.readInt();   // total size, not trusted: a truncated file over-reports it

    char wave[4] = {};
    if (in.read (wave, 4) != 4 || std::memcmp (wave, "WAVE", 4) != 0)
        return 0.0;

    juce::uint32 byteRate = 0;
    juce::int64 dataBytes = -1;

    // Chunks in order, until both facts are in hand or the file runs out. The
    // 8-byte guard is the header itself: fewer than that remaining means there
    // is no next chunk, only padding or damage.
    while (! in.isExhausted() && (in.getTotalLength() - in.getPosition()) >= 8)
    {
        char id[4] = {};

        if (in.read (id, 4) != 4)
            break;

        const juce::int64 size = (juce::uint32) in.readInt();
        const juce::int64 body = in.getPosition();

        if (std::memcmp (id, "fmt ", 4) == 0 && size >= 16)
        {
            in.readShort();                 // format tag
            in.readShort();                 // channels
            in.readInt();                   // sample rate
            byteRate = (juce::uint32) in.readInt();
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            // The SMALLER of the declared size and what is actually there. A
            // download cut short mid-write declares the full length it meant to
            // have, and reporting that would tell the user a truncated file is
            // the duration they asked for.
            dataBytes = juce::jmin (size, in.getTotalLength() - body);
        }

        if (byteRate > 0 && dataBytes >= 0)
            break;

        // Chunks are word-aligned: an odd size carries one pad byte that is not
        // counted in it.
        const juce::int64 next = body + size + (size & 1);

        if (next <= body || ! in.setPosition (next))
            break;
    }

    if (byteRate == 0 || dataBytes <= 0)
        return 0.0;

    return (double) dataBytes / (double) byteRate;
}

/** The places a delivered render can be, in the order to look.

    TWO on iOS in the container app, and that is the point. drain() MOVES files
    out of the outbox into Documents/Renders, so a process that only reads the
    outbox sees nothing after a drain -- which is exactly what happened: the
    standalone app drained its own outbox at launch and then showed an empty
    Sounds tab while the file sat in Files.app.

    In the APPEX only the outbox resolves, because the container app's Documents
    is a different sandbox. So the appex lists takes that have not been drained
    yet, which is all it can see and all it needs to: it is where they are made.

    On macOS the two are the same directory and the second is skipped.
*/
std::vector<juce::File> searchRoots()
{
    std::vector<juce::File> roots { outbox() };

    const auto delivered = deliveredRoot();

    if (delivered != outbox())
        roots.push_back (delivered);

    return roots;
}

juce::File resolve (const juce::String& fileName)
{
    // Every rejection here is a name the page should never have produced, since
    // the page gets its names from list() below. Refusing rather than sanitising
    // is deliberate: a sanitised name resolves to SOME file, and quietly serving
    // or deleting a different one than was asked for is worse than an error.
    if (fileName.isEmpty()
        || fileName.containsChar ('/')
        || fileName.containsChar ('\\')
        || fileName.contains ("..")
        || fileName.startsWithChar ('.')
        || ! fileName.endsWithIgnoreCase (".wav"))
    {
        return {};
    }

    // Both roots, because a name that came from list() may name a file the
    // drain has already moved. Without this, Play and Delete worked in the
    // appex and silently failed in the container app for the same sound.
    for (const auto& root : searchRoots())
    {
        const auto candidate = root.getChildFile (fileName);

        // Belt and braces against anything the checks above did not imagine:
        // the resolved path must still be a direct child of a root.
        if (candidate.getParentDirectory() == root && candidate.existsAsFile())
            return candidate;
    }

    return {};
}

std::vector<Entry> list (int max)
{
    std::vector<Entry> out;

    if (max <= 0)
        return out;

    // Every root, then sorted as one. Sorting per directory and concatenating
    // would put all of one folder's files ahead of the other's regardless of
    // age -- and after a drain the delivered folder holds everything OLD while
    // the outbox holds the newest, so the list would come out backwards.
    juce::Array<juce::File> files;

    for (const auto& dir : searchRoots())
        if (dir.isDirectory())
            dir.findChildFiles (files, juce::File::findFiles, false, "*.wav");

    ByNewestFirst comparator;
    files.sort (comparator, true);

    for (const auto& f : files)
    {
        if ((int) out.size() >= max)
            break;

        if (f.isHidden())
            continue;

        // A failed drain can leave the same name in both roots. Show it once:
        // searchRoots puts the outbox first, but either copy plays and drags
        // identically, so which one wins does not matter.
        const auto name = f.getFileName();
        const bool seen = std::any_of (out.begin(), out.end(),
            [&name] (const Entry& e) { return e.file.getFileName() == name; });

        if (seen)
            continue;

        Entry e;
        e.file = f;
        e.name = f.getFileNameWithoutExtension();
        e.bytes = f.getSize();
        e.modifiedMs = f.getLastModificationTime().toMilliseconds();

        // Read per entry rather than cached. It is a few dozen bytes per file
        // and the list is capped; a cache keyed on a path would be one more
        // thing that can be stale about a directory other processes write to.
        e.seconds = wavSeconds (f);

        out.push_back (std::move (e));
    }

    return out;
}

} // namespace kromosynth::renders
