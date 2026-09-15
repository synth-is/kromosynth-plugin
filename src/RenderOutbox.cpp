#include "RenderOutbox.h"

#include "InstrumentLibrary.h"

namespace kromosynth::renders
{

namespace
{

constexpr const char* kFolderName  = "Renders";
constexpr const char* kStagingName = ".incoming";

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

} // namespace kromosynth::renders
