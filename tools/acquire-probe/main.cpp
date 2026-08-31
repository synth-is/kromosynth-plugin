// acquire-probe — developer tool, never shipped.
//
// Drives ArtifactAcquisition with no plugin, no host, no webview and no audio
// device. Same conventions as sfz-probe and engine-probe: no COPY_AFTER_BUILD,
// no install rule.
//
// It exists because the acquisition path has three archive shapes and a fistful
// of failure modes, and the alternative way to exercise them is to build the
// plugin, re-sign it, re-notarise it and click things in GarageBand. Building
// the plugin also un-notarises the installed bundle, so a probe is not merely
// more convenient here -- it avoids destroying the state STAGE0_FINDINGS S M
// was closed on.
//
//   acquire-probe parse    <url>
//   acquire-probe install  <zip> <id> <libraryRoot>
//   acquire-probe fetch    <base> <id> <file> <ticket> <libraryRoot>
//   acquire-probe selftest <scratchDir>

#include "../../src/ArtifactAcquisition.h"

#include <juce_core/juce_core.h>

#include <iostream>
#include <vector>

using namespace kromosynth::acquisition;

namespace
{

int fail (const juce::String& why)
{
    std::cout << "FAIL  " << why << std::endl;
    return 1;
}

void line (const juce::String& s) { std::cout << s << std::endl; }

int checks = 0;
int failures = 0;

void check (bool condition, const juce::String& what)
{
    ++checks;

    if (! condition)
        ++failures;

    std::cout << (condition ? "  ok   " : "  FAIL ") << what << std::endl;
}

/** Writes a ZIP with the given entry paths, each holding trivial content. */
bool makeZip (const juce::File& out, const juce::StringArray& entryPaths,
              juce::OwnedArray<juce::MemoryBlock>& blocks)
{
    out.deleteFile();

    juce::ZipFile::Builder builder;

    for (const auto& path : entryPaths)
    {
        auto* block = blocks.add (new juce::MemoryBlock());
        const juce::String content = path.endsWithIgnoreCase (".sfz")
            ? "// Kromosynth Virtual Instrument\n// Sounds: PROBEFIXTURE\n<region> sample=samples/a.wav\n"
            : "fixture";
        block->append (content.toRawUTF8(), content.getNumBytesAsUTF8());
        builder.addEntry (new juce::MemoryInputStream (*block, false), 9, path,
                          juce::Time::getCurrentTime());
    }

    juce::FileOutputStream stream (out);

    if (! stream.openedOk())
        return false;

    return builder.writeToStream (stream, nullptr);
}

int selftest (const juce::File& scratch)
{
    scratch.deleteRecursively();

    if (! scratch.createDirectory())
        return fail ("could not create the scratch directory");

    const auto zips = scratch.getChildFile ("zips");
    zips.createDirectory();

    juce::OwnedArray<juce::MemoryBlock> blocks;

    line ("sentinel parsing");
    {
        const juce::String good = "synthis://acquire?artifact=art-1&file=pack.zip&ticket=v1.abc.def";

        check (isSentinel (good), "recognises the custom scheme");
        check (! isSentinel ("https://synth.is/garden"), "ignores an ordinary navigation");

        auto parsed = parseSentinel (good);
        check (parsed.has_value(), "parses a well-formed sentinel");

        if (parsed.has_value())
        {
            check (parsed->artifactId == "art-1", "reads the artefact id");
            check (parsed->fileName == "pack.zip", "reads the file name");
            check (parsed->ticket == "v1.abc.def", "reads the ticket");
        }

        check (! parseSentinel ("synthis://acquire?artifact=art-1&file=pack.zip").has_value(),
               "rejects a sentinel with no ticket");
        check (! parseSentinel ("synthis://acquire?artifact=../../etc&file=p.zip&ticket=t").has_value(),
               "rejects a traversing artefact id");
        check (! parseSentinel ("synthis://acquire?artifact=a&file=../x&ticket=t").has_value(),
               "rejects a traversing file name");
        check (! parseSentinel ("synthis://somethingelse?artifact=a&file=f&ticket=t").has_value(),
               "rejects an unknown verb on a known scheme");
        check (parseSentinel ("https://synth.is/__synthis_plugin/acquire?artifact=a&file=f.zip&ticket=t")
                   .has_value(),
               "accepts the documented https fallback");

        // Names carry spaces and a '#'. Percent-decoding them is juce::URL's
        // job, and this checks it is actually happening rather than the raw
        // escapes landing in the library list.
        {
            auto named = parseSentinel (
                "synthis://acquire?artifact=a&file=f.zip&ticket=t"
                "&name=Virtual%20Instrument%20%23JP9YS");
            check (named.has_value() && named->name == "Virtual Instrument #JP9YS",
                   "decodes a display name with spaces and a hash");

            auto unnamed = parseSentinel ("synthis://acquire?artifact=a&file=f.zip&ticket=t");
            check (unnamed.has_value() && unnamed->name.isEmpty(),
                   "a sentinel with no name is still valid");

            auto nasty = parseSentinel (
                "synthis://acquire?artifact=a&file=f.zip&ticket=t&name=%09bad%0Aname");
            check (nasty.has_value() && ! nasty->name.containsChar ('\n')
                       && ! nasty->name.containsChar ('\t'),
                   "strips control characters from a display name");
        }

        const auto redacted = redactForLog (good);
        check (! redacted.contains ("v1.abc.def"), "redaction removes the ticket value");
        check (! redacted.contains ("abc"), "redaction leaves no fragment of it");
        check (redacted.contains ("art-1"), "redaction keeps the artefact id");
        check (redacted.contains ("ticket=present"), "redaction records that a ticket was there");
        check (redactForLog ("https://synth.is/x") == "https://synth.is/x",
               "redaction leaves ordinary urls alone");
    }

    line ("");
    line ("archive shapes");
    {
        struct Shape
        {
            const char* label;
            juce::StringArray entries;
            bool shouldInstall;
        };

        const std::vector<Shape> shapes {
            { "single-format sfz at root",
              { "instrument.sfz", "samples/a.wav", "metadata.json", "README.txt" }, true },
            { "multi-format, sfz nested",
              { "sfz/instrument.sfz", "sfz/samples/a.wav", "decentsampler/p.dspreset", "metadata.json" }, true },
            { "non-sfz primary, no sfz anywhere",
              { "p.dspreset", "Samples/a.wav", "metadata.json" }, false },
        };

        int index = 0;

        for (const auto& shape : shapes)
        {
            const juce::String id = "shape-" + juce::String (index++);
            const auto zip = zips.getChildFile (id + ".zip");

            if (! makeZip (zip, shape.entries, blocks))
            {
                check (false, juce::String ("could not build fixture: ") + shape.label);
                continue;
            }

            const auto libraryRoot = scratch.getChildFile ("library");
            libraryRoot.createDirectory();

            const auto result = installFromArchive (zip, id, libraryRoot);

            check (result.ok == shape.shouldInstall,
                   juce::String (shape.label) + " -> "
                       + (result.ok ? juce::String ("installed") : "refused: " + result.message));

            if (shape.shouldInstall && result.ok)
            {
                const auto dir = libraryRoot.getChildFile (id);
                check (dir.getChildFile ("instrument.sfz").existsAsFile(),
                       juce::String (shape.label) + ": instrument.sfz landed at the top level");
                check (dir.getChildFile ("samples").isDirectory(),
                       juce::String (shape.label) + ": samples/ came with it");
                check (! dir.getChildFile ("sfz").exists(),
                       juce::String (shape.label) + ": the nesting was flattened away");
                check (! dir.getChildFile ("decentsampler").exists(),
                       juce::String (shape.label) + ": other formats were not extracted");
            }
        }
    }

    line ("");
    line ("install behaviour");
    {
        const auto libraryRoot = scratch.getChildFile ("library2");
        libraryRoot.createDirectory();

        const auto zip = zips.getChildFile ("replace.zip");
        makeZip (zip, { "instrument.sfz", "samples/a.wav" }, blocks);

        check (installFromArchive (zip, "replace-me", libraryRoot).ok, "first install succeeds");

        // A marker that must not survive a replacement, so "replaced" is
        // distinguishable from "merged into the old directory".
        //
        // Its creation is checked. On the first run of this selftest the marker
        // silently failed to appear -- replaceWithText does not create parent
        // directories, and the install above had failed -- so the absence check
        // below passed while proving nothing. A check that cannot fail is the
        // exact shape this project keeps paying for, and it appeared here in
        // the code written to catch it.
        const auto marker = libraryRoot.getChildFile ("replace-me").getChildFile ("stale.txt");
        marker.replaceWithText ("left over");
        check (marker.existsAsFile(), "the replacement marker was actually written");

        const auto second = installFromArchive (zip, "replace-me", libraryRoot);
        check (second.ok, "re-install succeeds");
        check (! marker.existsAsFile(), "re-install replaces rather than merges");
        check (libraryRoot.getChildFile ("replace-me").getChildFile ("instrument.sfz").existsAsFile(),
               "re-install leaves a working instrument");

        check (! installFromArchive (zips.getChildFile ("nope.zip"), "x", libraryRoot).ok,
               "a missing archive is refused");

        const auto notAZip = zips.getChildFile ("notazip.zip");
        notAZip.replaceWithText ("this is not a zip file");
        check (! installFromArchive (notAZip, "x", libraryRoot).ok, "a non-ZIP is refused");

        check (! installFromArchive (zip, "../escape", libraryRoot).ok,
               "a traversing artefact id is refused at install time too");

        check (! libraryRoot.getChildFile (".incoming").exists(),
               "staging is cleaned up after a successful run");

        // The failure path, which the previous version of this selftest did not
        // cover at all -- it only ever called installFromArchive, so the
        // staging directory fetchAndInstall creates was never in play. An
        // end-to-end run found three empty .incoming directories left in the
        // library after three refused downloads.
        //
        // An unroutable base URL exercises it without a server: the fetch fails
        // early, and what matters is what it leaves behind.
        {
            const auto failLib = scratch.getChildFile ("library3");
            failLib.createDirectory();

            Request bogus;
            bogus.artifactId = "never-arrives";
            bogus.fileName = "pack.zip";
            bogus.ticket = "v1.aaa.bbb";

            const auto failed = fetchAndInstall (bogus, "http://127.0.0.1:9", failLib);

            check (! failed.ok, "a fetch against a dead endpoint fails");
            check (! failLib.getChildFile (".incoming").exists(),
                   "a failed fetch leaves no staging directory behind");
        }

        // Finder drops a .DS_Store into directories it looks at, including the
        // staging directory. JUCE counts hidden files unless told not to, so
        // that one file made the emptiness check permanently false and the
        // staging directory survived every download on a real machine.
        //
        // The earlier version of this selftest could not have caught it: a
        // scratch directory under /tmp never acquires a .DS_Store, so the test
        // environment did not share the failure mode with the thing it stood in
        // for. Planting one is the whole point of this case.
        {
            const auto dsLib = scratch.getChildFile ("library4");
            dsLib.createDirectory();

            const auto zip = zips.getChildFile ("dsstore.zip");
            makeZip (zip, { "instrument.sfz", "samples/a.wav" }, blocks);

            const auto staging = dsLib.getChildFile (".incoming");
            staging.createDirectory();
            staging.getChildFile (".DS_Store").replaceWithText ("finder litter");
            check (staging.getChildFile (".DS_Store").existsAsFile(),
                   "the .DS_Store fixture was actually written");

            check (installFromArchive (zip, "ds-victim", dsLib).ok,
                   "install succeeds with Finder litter in staging");
            check (! staging.exists(),
                   "staging is removed even when it holds only a .DS_Store");
        }

        // The naming sidecar, end to end: written at install, then read back
        // through the same code the library list uses.
        {
            const auto nameLib = scratch.getChildFile ("library5");
            nameLib.createDirectory();

            const auto zip = zips.getChildFile ("named.zip");
            makeZip (zip, { "instrument.sfz", "samples/a.wav" }, blocks);

            const juce::String label = "Virtual Instrument #JP9YS";
            check (installFromArchive (zip, "named-1", nameLib, label).ok,
                   "installs with a display name");

            const auto dir = nameLib.getChildFile ("named-1");
            check (dir.getChildFile ("synthis.json").existsAsFile(), "sidecar written");
            check (InstrumentLibrary::readSidecarName (dir) == label,
                   "the library reads back the name the site sent");

            // Without a name the sidecar must not appear at all, or every
            // hand-placed instrument would gain an empty one.
            check (installFromArchive (zip, "named-2", nameLib).ok, "installs without a name");
            check (! nameLib.getChildFile ("named-2").getChildFile ("synthis.json").exists(),
                   "no sidecar when no name was supplied");
            check (InstrumentLibrary::readSidecarName (nameLib.getChildFile ("named-2")).isEmpty(),
                   "absent sidecar reads as empty so the caller falls back");

            // A corrupt sidecar must cost the label, not the instrument.
            const auto broken = nameLib.getChildFile ("named-1").getChildFile ("synthis.json");
            broken.replaceWithText ("{ this is not json");
            check (InstrumentLibrary::readSidecarName (nameLib.getChildFile ("named-1")).isEmpty(),
                   "a malformed sidecar reads as empty rather than throwing");
        }
    }

    line ("");
    line (juce::String (checks - failures) + "/" + juce::String (checks) + " checks passed");

    return failures == 0 ? 0 : 1;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String (argv[i]));

    if (args.isEmpty())
    {
        line ("acquire-probe parse    <url>");
        line ("acquire-probe install  <zip> <id> <libraryRoot>");
        line ("acquire-probe fetch    <base> <id> <file> <ticket> <libraryRoot>");
        line ("acquire-probe selftest <scratchDir>");
        return 2;
    }

    const auto command = args[0];

    if (command == "selftest")
        return args.size() >= 2 ? selftest (juce::File (args[1]))
                                : fail ("selftest needs a scratch directory");

    if (command == "parse")
    {
        if (args.size() < 2)
            return fail ("parse needs a url");

        line ("isSentinel : " + juce::String (isSentinel (args[1]) ? "yes" : "no"));
        line ("redacted   : " + redactForLog (args[1]));

        if (auto r = parseSentinel (args[1]))
            line ("parsed     : artifact=" + r->artifactId + " file=" + r->fileName
                  + " ticket=<" + juce::String (r->ticket.length()) + " chars, not printed>");
        else
            line ("parsed     : (rejected)");

        return 0;
    }

    if (command == "install")
    {
        if (args.size() < 4)
            return fail ("install needs <zip> <id> <libraryRoot>");

        const auto r = installFromArchive (juce::File (args[1]), args[2], juce::File (args[3]));
        line (juce::String (r.ok ? "OK    " : "FAIL  ") + r.message);
        return r.ok ? 0 : 1;
    }

    if (command == "fetch")
    {
        if (args.size() < 6)
            return fail ("fetch needs <base> <id> <file> <ticket> <libraryRoot>");

        Request request;
        request.artifactId = args[2];
        request.fileName = args[3];
        request.ticket = args[4];

        const auto r = fetchAndInstall (request, args[1], juce::File (args[5]),
                                        [] (double f)
                                        {
                                            if (f >= 0.0)
                                                std::cout << "\r  " << (int) (f * 100.0) << "%   " << std::flush;
                                        });

        std::cout << std::endl;
        line (juce::String (r.ok ? "OK    " : "FAIL  ") + r.message);
        return r.ok ? 0 : 1;
    }

    return fail ("unknown command: " + command);
}
