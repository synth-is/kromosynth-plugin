// RenderOutbox — where a single rendered sound lands, and how it gets out.
//
// C41, and the constraint that shapes all of it: THE APPEX CANNOT WRITE TO THE
// CONTAINER APP'S `Documents`. Separate sandboxes -- the same wall that put the
// instrument library in the App Group. So the obvious implementation ("write
// the WAV to Documents, where Files.app shows it") works from the standalone
// app and silently does nothing useful from the AUv3, which is the path that
// matters: nobody renders a sound from the standalone app and then wants it in
// a DAW, they are already IN the DAW.
//
// ── The shape ────────────────────────────────────────────────────────────────
//
//     <container>/Renders/           the OUTBOX. Both processes can write here.
//     <Documents>/Renders/           iOS only. Where Files.app and DAWs look.
//
// The container app DRAINS the outbox into Documents at launch: each file is
// MOVED, not copied. That choice is deliberate and is the whole reason this is
// simple.
//
// A copy would leave a second copy behind and then owe an answer to "what
// happens when the user moves or deletes the one in Documents?" -- re-copy it
// next launch (surprising), remember that it was deleted (a database), or
// diverge silently (worst). A move has no such question: the outbox is empty
// afterwards, and the file in Documents is the only one, owned by the user.
//
// WHAT IT COSTS, stated rather than discovered: a sound rendered in AUM is not
// in Files.app until Synth.is is next opened, and the appex can never list what
// it has already delivered, because it no longer has it. Both are accepted for
// now. If the staleness turns out to feel wrong in use, the better foundation
// is a document picker in export mode presented from the appex -- per-file,
// needs UI, but no sync question at all. That is an iOS-conventional answer and
// this is not a step away from it: the sentinel, the download and the naming
// below are all reused by it unchanged.
//
// ── macOS ────────────────────────────────────────────────────────────────────
//
// There is no appex/container split, so there is nothing to drain: the outbox
// IS the delivered location and it is ~/Music/Synth.is/Renders, visible in
// Finder next to the instruments. drain() compiles to a no-op returning zero.

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace kromosynth::renders
{

/** Where a freshly downloaded render is written, by either process.

    A sibling of InstrumentLibrary::root() under the same container, resolved
    through InstrumentLibrary::containerRoot() rather than independently -- see
    the note there on why a second resolver would be a bug waiting to happen.

    Not created here. Asking where a directory is should not make one.
*/
juce::File outbox();

/** Where a render ends up once it is reachable by the user.

    On iOS, `Documents/Renders` in the CONTAINER APP -- which is why calling
    this from the appex tells you where files are going, not where this process
    can write. On macOS it is outbox(), because they are the same place.
*/
juce::File deliveredRoot();

/** Staging for an in-flight download, inside the outbox so the final step is a
    rename on one volume rather than a copy across two.

    Hidden, and drain() skips hidden entries, so a download in progress in the
    appex cannot be half-moved by a container app launching at the wrong moment.
*/
juce::File staging();

/** A legal, recognisable `.wav` file name for a delivered render.

    @param displayName   the label the SITE showed on the card that was clicked.
                         Composed there rather than here for the same reason the
                         acquire sentinel's name is: only that side knows what
                         was on screen, and a plugin inventing its own label
                         from the same artefact would show a different string
                         than the page did.
    @param fallbackName  the artefact's own file name, e.g. `VI_a1b2c3_C2-C5.wav`
                         -- always safe, never descriptive. Used when the site
                         sent no name.

    THE SAME RENDER MUST PRODUCE THE SAME NAME. The site puts the takeout entry
    id in the label, so a re-download overwrites rather than accumulating
    "Bell (2).wav" beside an identical "Bell.wav". That matches the rule the
    quota code already follows -- a re-download of an artefact you have already
    taken is free, so it should not cost you a duplicate file either.
*/
juce::String fileNameFor (const juce::String& displayName,
                          const juce::String& fallbackName);

struct DrainResult
{
    int moved = 0;
    int failed = 0;

    /** For the log line. Empty when nothing moved. */
    juce::String firstName;
};

/** Moves everything in the outbox into deliveredRoot(). iOS only; a no-op that
    reports zero everywhere else.

    CALL THIS FROM THE CONTAINER APP ONLY. From the appex it would resolve a
    `Documents` this process can see and the app cannot, scattering renders into
    a sandbox nobody looks in. The caller checks the wrapper type; this function
    deliberately does not guess, because a wrong guess here is invisible.

    Blocking file I/O. Not for the message thread and certainly not for the
    audio thread.
*/
DrainResult drain();

/** One delivered render, as the shell page needs to show it. */
struct Entry
{
    juce::File file;
    juce::String name;        ///< file name without the extension
    double seconds = 0.0;     ///< 0 when the header could not be read
    juce::int64 bytes = 0;
    juce::int64 modifiedMs = 0;
};

/** The outbox's `.wav` files, newest first.

    Written for the shell page's Renders view, which is the thing that finally
    makes a "taken" marker an observation rather than an assumption -- a web
    card cannot see the disk, so it can only guess, and both the existing
    markers do exactly that.

    RenderList is now the only other reader, and it calls this rather than
    scanning the directory itself -- so there is one answer about what has been
    delivered, shared by the native list and by the page's Sounds badge.

    @param max  hard cap, so a folder that has grown for months cannot make the
                page slow. Newest first is what makes a cap acceptable.
*/
std::vector<Entry> list (int max = 64);

/** Duration of a PCM WAV in seconds; 0 when it cannot be determined.

    A RIFF CHUNK WALK RATHER THAN juce::WavAudioFormat, and the reason is a link
    dependency rather than taste: tools/acquire-probe links this file against
    juce_core alone, so reaching for juce_audio_formats here would break the one
    test harness that can exercise it without a DAW. The format is stable enough
    to read forty bytes of by hand, and doing so keeps this file's dependencies
    where they were.

    Chunks are walked rather than assuming the canonical 44-byte header: our own
    writer produces that, but a file a user has dropped in by hand may carry a
    LIST or a cue chunk before `data`.
*/
double wavSeconds (const juce::File& f);

/** One file in the outbox, by FILE NAME alone.

    The gate for everything the page can name: a resource route serving bytes,
    and a delete. Returns a nonexistent File for anything that is not a plain
    `.wav` leaf name -- no separators, no `..`, nothing hidden. The page supplies
    these names from a list this module produced, so a name that fails here is
    either a bug or an attempt, and both want the same answer.
*/
juce::File resolve (const juce::String& fileName);

} // namespace kromosynth::renders
