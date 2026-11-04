// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
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
//   acquire-probe install  <zip> <id> <libraryRoot> [displayName]
//   acquire-probe fetch    <base> <id> <file> <ticket> <libraryRoot>
//   acquire-probe selftest <scratchDir>

#include "../../src/ArtifactAcquisition.h"
// For fileNameFor, which decides what a delivered render is CALLED. That rule
// has five branches and no server in it, so it is exactly the sort of thing
// this probe exists to pin down.
#include "../../src/RenderOutbox.h"
// The render-progress sentinel and the activity list it feeds (synthis-7o3.2).
// Both header-only and juce_core-only, so the probe can exercise them with no
// window, exactly like the acquisition parser above.
#include "../../src/ProgressSentinel.h"
#include "../../src/ActivityList.h"

#include <juce_core/juce_core.h>

#include <cmath>
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

/** Writes a ZIP with the given entry paths, each holding trivial content.

    `overrides` supplies real content for entries whose content is actually
    read -- synthis.json, which the merge case below depends on being parseable.
    Everything else can stay a placeholder: installFromArchive moves bytes and
    does not interpret a .wasm or a .sfz.
*/
bool makeZip (const juce::File& out, const juce::StringArray& entryPaths,
              juce::OwnedArray<juce::MemoryBlock>& blocks,
              const juce::StringPairArray& overrides = {})
{
    out.deleteFile();

    juce::ZipFile::Builder builder;

    for (const auto& path : entryPaths)
    {
        auto* block = blocks.add (new juce::MemoryBlock());
        const juce::String content = overrides.containsKey (path)
            ? overrides[path]
            : (path.endsWithIgnoreCase (".sfz")
                ? "// Kromosynth Virtual Instrument\n// Sounds: PROBEFIXTURE\n<region> sample=samples/a.wav\n"
                : "fixture");
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
    line ("the render verb");
    {
        // C41. A single rendered sound is delivered as a FILE and not installed,
        // so it takes its own verb. Everything below is about keeping the two
        // apart -- a WAV that reached the install path would be handed to a ZIP
        // unpacker, which is the defect the verb exists to remove.
        auto rendered = parseSentinel (
            "synthis://render?artifact=art-2&file=VI_a1b2c3_C2-C5.wav&ticket=v1.abc.def");

        check (rendered.has_value(), "parses a render sentinel");
        check (rendered.has_value() && rendered->verb == Verb::render,
               "a render sentinel carries the render verb");
        check (rendered.has_value() && rendered->fileName == "VI_a1b2c3_C2-C5.wav",
               "the VI service's generated wav name survives isSafeComponent");

        auto acquired = parseSentinel ("synthis://acquire?artifact=a&file=f.zip&ticket=t");
        check (acquired.has_value() && acquired->verb == Verb::acquire,
               "an acquire sentinel still carries the acquire verb");

        auto fallback = parseSentinel (
            "https://synth.is/__synthis_plugin/render?artifact=a&file=f.wav&ticket=t");
        check (fallback.has_value() && fallback->verb == Verb::render,
               "the https fallback recognises the render verb too");

        // The auth sentinel shares the scheme and is handled earlier in
        // pageAboutToLoad. If it ever reached here, unparsed-and-blocked is the
        // right answer rather than a wrong one.
        check (! parseSentinel ("synthis://auth?state=registered&label=bj").has_value(),
               "the auth verb is not mistaken for an artefact request");

        line ("");
        line ("  naming a delivered render");

        check (kromosynth::renders::fileNameFor ("Synth.is JP9YS", "VI_x_C2-C5.wav")
                   == "Synth.is JP9YS.wav",
               "the site's label becomes the file name");
        check (kromosynth::renders::fileNameFor ("Bell.wav", "f.wav") == "Bell.wav",
               "a label already ending in .wav is not doubled");
        check (kromosynth::renders::fileNameFor ("a/b:c", "f.wav").isNotEmpty()
                   && ! kromosynth::renders::fileNameFor ("a/b:c", "f.wav").containsChar ('/'),
               "path separators are stripped from a label");
        check (kromosynth::renders::fileNameFor ("", "VI_x_C2-C5.wav") == "VI_x_C2-C5.wav",
               "an absent label falls back to the artefact's own name");
        check (kromosynth::renders::fileNameFor ("", "") == "render.wav",
               "neither name still produces a usable file name");
        check (kromosynth::renders::fileNameFor (juce::String::repeatedString ("x", 400), "f.wav")
                   .length() <= 96,
               "an absurd label is truncated rather than passed to the filesystem");

        line ("");
        line ("  the two paths refuse each other's work");

        const auto mixedRoot = scratch.getChildFile ("verb-guard");
        mixedRoot.createDirectory();

        Request asAcquire;
        asAcquire.verb = Verb::acquire;
        asAcquire.artifactId = "a";
        asAcquire.fileName = "pack.zip";
        asAcquire.ticket = "t";

        Request asRender = asAcquire;
        asRender.verb = Verb::render;
        asRender.fileName = "one.wav";

        // Refused BEFORE any network call, so an unroutable base is irrelevant
        // to what is being checked -- and the check would be worthless if it
        // passed only because the socket failed.
        check (! fetchRender (asAcquire, "http://127.0.0.1:9", mixedRoot).ok,
               "fetchRender refuses an acquire request");
        check (! fetchAndInstall (asRender, "http://127.0.0.1:9", mixedRoot).ok,
               "fetchAndInstall refuses a render request");
        check (mixedRoot.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0,
               "a refused request touches nothing on disk");

        // The staging-hygiene case, for the second path. fetchAndInstall's
        // version of this caught three empty .incoming directories left behind
        // by three refused downloads; the render path creates the same
        // directory and has the same eight early returns.
        {
            const auto outbox = scratch.getChildFile ("outbox-fail");
            outbox.createDirectory();

            Request bogus;
            bogus.verb = Verb::render;
            bogus.artifactId = "never-arrives";
            bogus.fileName = "one.wav";
            bogus.ticket = "v1.aaa.bbb";

            check (! fetchRender (bogus, "http://127.0.0.1:9", outbox).ok,
                   "a render fetch against a dead endpoint fails");
            check (! outbox.getChildFile (".incoming").exists(),
                   "a failed render fetch leaves no staging directory behind");
        }

        line ("");
        line ("  reading a wav's duration");

        // wavSeconds walks RIFF chunks with juce_core alone, because this probe
        // links RenderOutbox against juce_core alone. Checked against files
        // written by the same createWavFromFloat32 the render pipeline uses, so
        // the header shape here is the one that occurs in production.
        {
            const auto made = scratch.getChildFile ("probe-tone.wav");

            // 48 kHz, 2ch, 16-bit, 0.5s => byteRate 192000, data 96000 bytes.
            const juce::uint32 sampleRate = 48000;
            const juce::uint16 channels = 2;
            const juce::uint16 bits = 16;
            const juce::uint32 byteRate = sampleRate * channels * (bits / 8);
            const juce::uint32 dataBytes = byteRate / 2;

            juce::MemoryOutputStream m;
            m.write ("RIFF", 4);
            m.writeInt ((int) (36 + dataBytes));
            m.write ("WAVE", 4);
            m.write ("fmt ", 4);
            m.writeInt (16);
            m.writeShort (1);                       // PCM
            m.writeShort ((short) channels);
            m.writeInt ((int) sampleRate);
            m.writeInt ((int) byteRate);
            m.writeShort ((short) (channels * bits / 8));
            m.writeShort ((short) bits);
            m.write ("data", 4);
            m.writeInt ((int) dataBytes);
            for (juce::uint32 i = 0; i < dataBytes; ++i) m.writeByte (0);

            made.replaceWithData (m.getData(), m.getDataSize());

            const auto secs = kromosynth::renders::wavSeconds (made);
            check (std::abs (secs - 0.5) < 1.0e-6,
                   "a canonical 44-byte header reads as its true duration");

            // TRUNCATION MUST NOT BE BELIEVED. A download cut short declares the
            // full length it meant to have; reporting that would tell the user a
            // half-written file is the length they asked for -- the same class of
            // lie as the artefact metadata claiming 9.0s for a 1.7s render (C44).
            const auto cut = scratch.getChildFile ("probe-truncated.wav");
            cut.replaceWithData (m.getData(), m.getDataSize() - (dataBytes / 2));

            const auto cutSecs = kromosynth::renders::wavSeconds (cut);
            check (cutSecs > 0.0 && cutSecs < 0.3,
                   "a truncated wav reports what is THERE, not what it claims");

            check (kromosynth::renders::wavSeconds (scratch.getChildFile ("nope.wav")) == 0.0,
                   "a missing file is 0 seconds rather than a crash");

            const auto notWav = scratch.getChildFile ("probe-notawav.wav");
            notWav.replaceWithText ("this is not a RIFF file at all");
            check (kromosynth::renders::wavSeconds (notWav) == 0.0,
                   "a non-RIFF file is refused rather than misread");
        }

        line ("");
        line ("  resolving a name the page supplied");

        // Everything the shell page can name goes through resolve(): a resource
        // route serving bytes, and a delete. Refusing rather than sanitising is
        // the point -- a sanitised name resolves to SOME file, and quietly
        // deleting a different one than was asked for is worse than an error.
        check (! kromosynth::renders::resolve ("../../etc/passwd.wav").exists(),
               "a traversing name resolves to nothing");
        check (! kromosynth::renders::resolve ("a/b.wav").exists(),
               "a name with a separator resolves to nothing");
        check (! kromosynth::renders::resolve ("notes.txt").exists(),
               "a non-wav name resolves to nothing");
        check (! kromosynth::renders::resolve (".hidden.wav").exists(),
               "a hidden name resolves to nothing");
        check (! kromosynth::renders::resolve ("").exists(),
               "an empty name resolves to nothing");
    }

    line ("");
    line ("archive shapes");
    {
        // `present` and `absent` are per-shape rather than a fixed SFZ block,
        // because the two kinds land different files. ARTEFACT_CONTRACT §1.
        struct Shape
        {
            const char* label;
            juce::StringArray entries;
            bool shouldInstall;
            juce::StringArray present;
            juce::StringArray absent;
        };

        const std::vector<Shape> shapes {
            { "single-format sfz at root",
              { "instrument.sfz", "samples/a.wav", "metadata.json", "README.txt" }, true,
              { "instrument.sfz", "samples" }, { "sfz", "decentsampler" } },

            { "multi-format, sfz nested",
              { "sfz/instrument.sfz", "sfz/samples/a.wav", "decentsampler/p.dspreset", "metadata.json" }, true,
              { "instrument.sfz", "samples" }, { "sfz", "decentsampler" } },

            { "non-sfz primary, no sfz anywhere",
              { "p.dspreset", "Samples/a.wav", "metadata.json" }, false, {}, {} },

            // The Faust half of the contract. Nothing produces one of these
            // yet, which is exactly why the plugin's acceptance of them needs
            // a test that does not depend on the server existing.
            { "faust pair at root",
              { "instrument.wasm", "instrument.json", "README.txt" }, true,
              { "instrument.wasm", "instrument.json" }, {} },

            { "faust pair nested",
              { "faust/instrument.wasm", "faust/instrument.json", "metadata.json" }, true,
              { "instrument.wasm", "instrument.json" }, { "faust" } },

            // A module without its metadata is unplayable, not degraded: the
            // struct size and parameter byte offsets are only in the JSON.
            { "faust wasm with no json",
              { "instrument.wasm", "metadata.json" }, false, {}, {} },

            { "faust json with no wasm",
              { "instrument.json", "metadata.json" }, false, {}, {} },

            // A .json one level away from its .wasm is not "beside" it. Without
            // the prefix test this installed a folder the engine cannot load.
            { "faust wasm whose json is in another directory",
              { "instrument.wasm", "meta/instrument.json" }, false, {}, {} },

            // Suffix matching accepted this and picked a prefix from a file no
            // engine will ever open.
            { "a decoy leaf name is not an instrument",
              { "my-instrument.sfz", "samples/a.wav" }, false, {}, {} },

            // Shallowest wins ACROSS kinds, so a root-level Faust pair beats a
            // nested SFZ.
            { "faust at root outranks a nested sfz",
              { "instrument.wasm", "instrument.json", "sfz/instrument.sfz", "sfz/samples/a.wav" }, true,
              { "instrument.wasm", "instrument.json" }, { "sfz" } },

            // ...and at equal depth the sampler wins, matching the order
            // InstrumentLibrary::resolve tests in. If these disagreed, an
            // archive would install as one kind and load as the other.
            { "a nested faust pair loses to a root sfz",
              { "instrument.sfz", "samples/a.wav", "faust/instrument.wasm", "faust/instrument.json" }, true,
              { "instrument.sfz", "samples" }, { "faust" } },
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

                // These filenames ARE the agreement with the loader:
                // InstrumentLibrary::resolve decides the kind by testing for
                // exactly instrument.sfz, or instrument.wasm plus
                // instrument.json. Asserting they landed at the top level is
                // therefore the listing/loading agreement check (§O3), not
                // merely a file-existence one.
                //
                // resolve() itself cannot be called here: it resolves against
                // InstrumentLibrary::root(), not this scratch directory, so it
                // would answer `none` for every fixture and any check built on
                // it would pass without testing anything.
                for (const auto& expected : shape.present)
                    check (dir.getChildFile (expected).exists(),
                           juce::String (shape.label) + ": " + expected + " landed at the top level");

                for (const auto& unwanted : shape.absent)
                    check (! dir.getChildFile (unwanted).exists(),
                           juce::String (shape.label) + ": " + unwanted + " was not extracted");
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

        // The sidecar MERGE. ARTEFACT_CONTRACT §2 makes the server the first
        // writer of synthis.json -- provenance, dates, characteristics and the
        // measured block all arrive in it -- and the plugin then stamps the
        // display name into the same file. The previous implementation
        // overwrote it with three keys, which would have silently discarded
        // everything the artefact came with and left a library that can sort
        // only by name. Nothing would have failed; the fields would just never
        // have been there.
        {
            const auto mergeLib = scratch.getChildFile ("library6");
            mergeLib.createDirectory();

            const juce::String serverSidecar =
                R"({"schema":1,"kind":"faust",)"
                R"("name":"Named By The Server",)"
                R"("source":{"soundIds":["01KPXZJ94GPQEEAZFA6F91YR1W"],"generation":0,)"
                R"("substrate":"faust","discoveredAt":"2026-08-14T09:03:11.000Z",)"
                R"("adoptedAt":"2026-08-19T21:40:02.000Z"},)"
                R"("character":{"soundType":"textural","labelsAreCorpusRelative":true,)"
                R"("labels":{"brightness":"bright","noisiness":"tonal","rolloff":"airy",)"
                R"("energy":"moderate","percussive":"percussive","richness":"rich",)"
                R"("width":"narrow"},)"
                R"("descriptors":[0.61,0.12,0.77,0.34,0.81,0.66,0.29]},)"
                R"("measured":{"trim":0.593,"trimDb":-4.4,"rtf":112.2,)"
                R"("rtfPlatform":"v8-faustwasm","capAt4x":16,)"
                R"("articulation":"drone","hasGate":true}})";

            juce::StringPairArray overrides;
            overrides.set ("synthis.json", serverSidecar);

            const auto zip = zips.getChildFile ("merge.zip");
            makeZip (zip, { "instrument.wasm", "instrument.json", "synthis.json" },
                     blocks, overrides);

            const juce::String siteLabel = "Virtual Instrument #K3X35";

            check (installFromArchive (zip, "merged-1", mergeLib, siteLabel).ok,
                   "installs a faust artefact that already carries a sidecar");

            const auto meta = InstrumentLibrary::readSidecar (mergeLib.getChildFile ("merged-1"));

            check (meta.name == siteLabel, "the site's name wins over the server's");
            check (meta.acquiredAt.isNotEmpty(), "acquisition time was stamped in");
            check (meta.substrate == "faust", "substrate survived the stamp");
            check (meta.discoveredAt.isNotEmpty() && meta.adoptedAt.isNotEmpty(),
                   "both dates survived the stamp");
            check (meta.generation == 0,
                   "generation 0 reads as 0 rather than as absent");
            check (meta.soundType == "textural", "soundType survived the stamp");
            check (meta.labels[0] == "bright" && meta.labels[6] == "narrow",
                   "the first and last labels survived and are not shifted");
            check (meta.labelsAreCorpusRelative,
                   "the corpus-relative flag survived, so a reader knows what the labels claim");
            check (meta.hasDescriptors, "the descriptor vector survived");
            check (std::abs (meta.descriptors[6] - 0.29f) < 1.0e-5f,
                   "the seventh descriptor is the seventh");
            check (meta.hasMeasured && std::abs (meta.trimDb + 4.4f) < 0.05f,
                   "the measured block survived, so load need not re-measure");
            check (meta.articulation == "drone" && meta.hasGate,
                   "articulation and hasGate survived");
            check (meta.rtfPlatform == "v8-faustwasm",
                   "rtf carries the platform it was measured on");

            // No name from the site, but a sidecar in the archive: the id and
            // the acquisition time still have somewhere to go, and the server's
            // name must not be blanked by an empty one.
            check (installFromArchive (zip, "merged-2", mergeLib).ok,
                   "installs the same artefact with no display name");

            const auto unnamed = InstrumentLibrary::readSidecar (mergeLib.getChildFile ("merged-2"));

            check (unnamed.name == "Named By The Server",
                   "an absent site name leaves the server's name alone");
            check (unnamed.acquiredAt.isNotEmpty(),
                   "and the stamp still happens");

            // Six values where seven are required. The dimensions are
            // positional, so a short vector would shift `width` into
            // `richness` and every ordering built on it would be wrong while
            // looking fine. Refused whole rather than read as far as it goes.
            const auto shortVector = mergeLib.getChildFile ("short-vector");
            shortVector.createDirectory();
            shortVector.getChildFile ("synthis.json").replaceWithText (
                R"({"character":{"descriptors":[0.1,0.2,0.3,0.4,0.5,0.6]}})");

            check (! InstrumentLibrary::readSidecar (shortVector).hasDescriptors,
                   "a six-element descriptor vector is refused rather than partly read");
        }
    }

    line ("");
    line ("the progress sentinel (synthis-7o3.2)");
    {
        namespace prog = kromosynth::progress;

        const juce::String valid = "synthis://progress?job=job-1&phase=rendering&pct=42&title=Pluck%2014";

        check (prog::isProgressSentinel (valid), "recognises synthis://progress");
        check (! prog::isProgressSentinel ("synthis://progressive?job=a&phase=queued"),
               "a longer host segment does not match");
        check (! prog::isProgressSentinel ("synthis://acquire?artifact=a&file=f.zip&ticket=t"),
               "the acquire verb is not a progress sentinel");
        check (! prog::isProgressSentinel ("synthis://render?artifact=a&file=f.wav&ticket=t"),
               "the render verb is not a progress sentinel");
        check (! prog::isProgressSentinel ("synthis://auth?state=registered&label=bj"),
               "the auth verb is not a progress sentinel");
        check (! prog::isProgressSentinel ("synthis://checkout?url=https%3A%2F%2Fsynth.is%2Fpricing"),
               "the checkout verb is not a progress sentinel");
        check (! prog::isProgressSentinel ("https://synth.is/garden"),
               "an ordinary navigation is not a progress sentinel");

        // The greedy-match trap this branch defends against: acquisition's
        // isSentinel matches ANY synthis:// URL, which is why the progress
        // check has to run before it in pageAboutToLoad.
        check (isSentinel (valid),
               "acquisition::isSentinel greedily matches the progress verb too");
        check (! prog::parse ("synthis://acquire?artifact=a&file=f.zip&ticket=t").has_value(),
               "parse refuses the acquire verb");

        auto ev = prog::parse (valid);
        check (ev.has_value(), "parses a valid progress sentinel");
        if (ev.has_value())
        {
            check (ev->job == "job-1", "reads the job id");
            check (ev->phase == prog::Phase::rendering, "reads the phase");
            check (ev->pct == 42, "reads the percentage");
            check (ev->title == "Pluck 14", "decodes the title");
        }

        // Each site phase parses.
        check (prog::parse ("synthis://progress?job=j&phase=queued").has_value()
               && prog::parse ("synthis://progress?job=j&phase=queued")->phase == prog::Phase::queued,
               "phase=queued parses");
        check (prog::parse ("synthis://progress?job=j&phase=rendering&pct=7").has_value()
               && prog::parse ("synthis://progress?job=j&phase=rendering&pct=7")->phase == prog::Phase::rendering,
               "phase=rendering parses");
        {
            auto delivering = prog::parse ("synthis://progress?job=j&phase=delivering&artifact=art-9");
            check (delivering.has_value() && delivering->phase == prog::Phase::delivering
                   && delivering->artifact == "art-9",
                   "phase=delivering parses and carries the artifact join key");
        }
        {
            auto failed = prog::parse ("synthis://progress?job=j&phase=failed&reason=quota");
            check (failed.has_value() && failed->phase == prog::Phase::failed
                   && failed->reason == prog::Reason::quota
                   && prog::toWire (failed->reason) == "quota",
                   "phase=failed parses with its reason");
        }

        // An unknown phase is blocked but not parsed: a newer site may add
        // phases and an older plugin must not misread them.
        const juce::String unknownPhase = "synthis://progress?job=j&phase=downloading";
        check (prog::isProgressSentinel (unknownPhase), "an unknown phase is still blocked");
        check (! prog::parse (unknownPhase).has_value(), "an unknown phase is not parsed");
        check (prog::hasUnknownPhase (unknownPhase), "the caller can log it as unknown_phase");
        check (! prog::hasUnknownPhase (valid), "a known phase is not unknown_phase");

        // landed and lost are native-only: rejected by the parser, still blocked.
        check (prog::isProgressSentinel ("synthis://progress?job=j&phase=landed")
               && ! prog::parse ("synthis://progress?job=j&phase=landed").has_value(),
               "phase=landed is blocked and rejected (native-only)");
        check (prog::isProgressSentinel ("synthis://progress?job=j&phase=lost")
               && ! prog::parse ("synthis://progress?job=j&phase=lost").has_value(),
               "phase=lost is blocked and rejected (native-only)");

        // Missing fields.
        check (! prog::parse ("synthis://progress?phase=queued").has_value(),
               "a missing job is malformed");
        check (! prog::parse ("synthis://progress?job=j").has_value(),
               "a missing phase is malformed");

        // Job charset and length: [A-Za-z0-9_-]{1,64}, anything else malformed.
        check (! prog::parse ("synthis://progress?job=" + juce::String::repeatedString ("x", 65)
                              + "&phase=queued").has_value(),
               "a 65-character job is malformed");
        check (prog::parse ("synthis://progress?job=" + juce::String::repeatedString ("x", 64)
                            + "&phase=queued").has_value(),
               "a 64-character job is accepted");
        check (! prog::parse ("synthis://progress?job=bad%20id&phase=queued").has_value(),
               "a job with a space is malformed");
        check (! prog::parse ("synthis://progress?job=bad.id&phase=queued").has_value(),
               "a job with a dot is malformed");

        // Title: clamped, not rejected; control characters out.
        auto longTitle = prog::parse ("synthis://progress?job=j&phase=queued&title="
                                      + juce::String::repeatedString ("y", 100));
        check (longTitle.has_value() && longTitle->title.length() == prog::kMaxTitle,
               "an over-long title is clamped, not rejected");
        auto nastyTitle = prog::parse ("synthis://progress?job=j&phase=queued&title=%09bad%0Aname");
        check (nastyTitle.has_value() && ! nastyTitle->title.containsChar ('\n')
               && ! nastyTitle->title.containsChar ('\t'),
               "control characters are stripped from the title");

        // pct: clamped, rendering-only, absent means indeterminate.
        check (prog::parse ("synthis://progress?job=j&phase=rendering&pct=150")->pct == 100,
               "pct above 100 is clamped");
        check (prog::parse ("synthis://progress?job=j&phase=rendering&pct=-5")->pct == 0,
               "a negative pct is clamped to 0");
        check (prog::parse ("synthis://progress?job=j&phase=queued&pct=42")->pct == -1,
               "pct with a non-rendering phase is ignored");
        check (prog::parse ("synthis://progress?job=j&phase=rendering")->pct == -1,
               "an absent pct is indeterminate");
        check (! prog::parse ("synthis://progress?job=j&phase=rendering&pct=abc").has_value(),
               "a non-numeric pct is malformed");

        check (! prog::parse ("synthis://progress?job=j&phase=delivering").has_value(),
               "delivering without an artifact is malformed (the join key is required)");
    }

    line ("");
    line ("the activity list (synthis-7o3.2)");
    {
        namespace prog = kromosynth::progress;

        int64_t now = 100000;
        kromosynth::activity::List list ([&now] { return now; });
        list.setSiteHidden (false);   // the site view is showing

        auto send = [&] (const juce::String& url) -> bool
        {
            const auto ev = prog::parse (url);
            if (! ev.has_value())
            {
                check (false, juce::String ("test url parses: ") + url);
                return false;
            }
            return list.apply (*ev);
        };

        check (send ("synthis://progress?job=j1&phase=queued&title=Pluck%2014"),
               "a first event creates an entry");
        {
            const auto json = list.toJson();
            check ((int) json["active"] == 1, "one active entry");
            check (json["entries"].size() == 1, "one entry in the json");
            check (json["entries"][0]["job"].toString() == "j1"
                   && json["entries"][0]["phase"].toString() == "queued"
                   && json["entries"][0]["title"].toString() == "Pluck 14",
                   "the entry carries job, phase and title");
        }

        check (send ("synthis://progress?job=j1&phase=rendering&pct=40"), "a forward event applies");
        check (! send ("synthis://progress?job=j1&phase=queued"),
               "a lower rank is ignored (out-of-order event)");
        {
            const auto json = list.toJson();
            check (json["entries"][0]["phase"].toString() == "rendering"
                   && (int) json["entries"][0]["pct"] == 40,
                   "the entry stayed at rendering with its pct");
        }
        check (send ("synthis://progress?job=j1&phase=rendering&pct=55"), "a same-phase pct update applies");
        check (send ("synthis://progress?job=j1&phase=delivering&artifact=art-1"),
               "delivering applies and stores the artifact");
        {
            const auto json = list.toJson();
            check (json["entries"][0]["phase"].toString() == "delivering"
                   && json["entries"][0]["artifact"].toString() == "art-1",
                   "the artifact join key is stored");
        }

        // Lease: 60 s of silence drops the pct to indeterminate, 180 s loses the entry.
        send ("synthis://progress?job=j2&phase=rendering&pct=30&title=Q");
        now += 61 * 1000;
        check (list.tick(), "the 60 s silence boundary changes the list");
        check ((int) list.toJson()["entries"][1]["pct"] == -1,
               "pct is reported as -1 after 60 s of silence");
        now += 120 * 1000;   // 181 s since the last event
        check (list.tick(), "the 180 s silence boundary changes the list");
        check (list.toJson()["entries"][0]["phase"].toString() == "lost",
               "the entry is lost after 180 s of silence");

        // A landing revives a lost entry.
        check (list.land ("art-1", "Pluck 14", true), "a landing revives the lost entry");
        {
            const auto json = list.toJson();
            check (json["entries"][0]["phase"].toString() == "landed", "the revived entry is landed");
            check ((int) json["unseenLanded"] == 1, "the landing counts as unseen");
            check ((int) json["active"] == 0, "a landed entry is not active");
        }
        check (! send ("synthis://progress?job=j1&phase=rendering&pct=10"),
               "a terminal entry ignores sentinel events");

        // Terminal entries expire after 8 s.
        now += 8 * 1000;
        check (list.tick(), "the 8 s terminal expiry changes the list");
        check (list.toJson()["entries"].size() == 0, "the landed entry expired");

        // An acquisition with no known job still lands, titled from the request's name.
        check (list.land ("art-9", "Bell 3", true), "a landing with no known job creates an entry");
        {
            const auto json = list.toJson();
            check (json["entries"][0]["job"].toString() == "art-9"
                   && json["entries"][0]["title"].toString() == "Bell 3"
                   && json["entries"][0]["phase"].toString() == "landed",
                   "the new landed entry is titled from the request's name");
        }
        now += 8 * 1000;
        list.tick();

        // A failed acquisition marks the matching entry failed; a landing does
        // not revive a failed one.
        send ("synthis://progress?job=j2&phase=rendering&pct=10&title=X");
        send ("synthis://progress?job=j2&phase=delivering&artifact=art-2");
        check (list.fail ("art-2", "network"), "a failed acquisition marks the entry failed");
        {
            const auto json = list.toJson();
            check (json["entries"][0]["phase"].toString() == "failed"
                   && json["entries"][0]["reason"].toString() == "network",
                   "the failed entry carries the reason");
        }
        check (! list.land ("art-2", "X", true), "a landing does not revive a failed entry");
        now += 8 * 1000;
        list.tick();

        // No silence clock while the site view is hidden; it restarts from zero
        // when the site is shown again.
        send ("synthis://progress?job=j3&phase=rendering&pct=20&title=Y");
        now += 5 * 1000;
        list.setSiteHidden (true);
        now += 200 * 1000;
        check (! list.tick(), "no silence clock runs while the site view is hidden");
        check (list.toJson()["entries"][0]["phase"].toString() == "rendering",
               "the entry survives 200 s hidden");
        list.setSiteHidden (false);   // shown again: the clock restarts from zero
        now += 30 * 1000;
        check (! list.tick(), "the silence clock restarted from zero on show");
        now += 151 * 1000;            // 181 s since the show
        check (list.tick(), "the entry is lost 181 s after the site was shown again");
        check (list.toJson()["entries"][0]["phase"].toString() == "lost",
               "lost after the restarted clock ran out");
        now += 8 * 1000;
        list.tick();

        // A full load of the site view loses every non-terminal entry at once.
        send ("synthis://progress?job=j4&phase=queued&title=Z");
        send ("synthis://progress?job=j5&phase=rendering&pct=5&title=W");
        check (list.siteReloaded(), "a site reload changes the list");
        {
            const auto json = list.toJson();
            check (json["entries"][0]["phase"].toString() == "lost"
                   && json["entries"][1]["phase"].toString() == "lost",
                   "every non-terminal entry is lost at once");
        }
        now += 8 * 1000;
        list.tick();

        // The 25 min hard cap applies even with heartbeats renewing the lease.
        send ("synthis://progress?job=j6&phase=rendering&pct=1&title=H");
        for (int i = 0; i < 149; ++i)   // 149 * 10 s of heartbeats = 24 min 50 s
        {
            now += 10 * 1000;
            send ("synthis://progress?job=j6&phase=rendering&pct=1");
        }
        now += 10 * 1000;               // 25 min since the first event
        check (list.tick(), "the hard cap fires");
        check (list.toJson()["entries"][0]["phase"].toString() == "lost",
               "the entry is lost at the 25 min cap");

        // The badge count outlives the entries: a landed entry expires after
        // 8 s, but unseenLanded keeps counting until the Library is opened.
        {
            int64_t t = 900000;
            kromosynth::activity::List badge ([&t] { return t; });
            badge.setSiteHidden (false);

            check (badge.land ("b-1", "Pluck 14", true), "a landing with no known job lands");
            t += 8 * 1000;
            check (badge.tick(), "the landed entry expires after 8 s");
            check (badge.toJson()["entries"].size() == 0, "the entry is gone");
            check ((int) badge.toJson()["unseenLanded"] == 1,
                   "unseenLanded is still 1 after the entry expired");

            check (badge.land ("b-2", "Bell 3", true), "a second landing lands");
            check ((int) badge.toJson()["unseenLanded"] == 2, "a second landing counts: 2");
            t += 8 * 1000;
            badge.tick();
            check ((int) badge.toJson()["unseenLanded"] == 2,
                   "the count survives the second expiry too");

            check (badge.land ("b-3", "One-shot", false), "a non-instrument landing still lands");
            check ((int) badge.toJson()["unseenLanded"] == 2,
                   "a landing with instrument=false does not count");

            check (badge.land ("b-4", "Pad 2", true), "a fourth landing lands");
            check ((int) badge.toJson()["unseenLanded"] == 3, "it counts: 3");
            check (! badge.land ("b-4", "Pad 2", true),
                   "landing an already-landed artefact returns false");
            check ((int) badge.toJson()["unseenLanded"] == 3,
                   "a repeated landing does not count again");

            check (badge.clearUnseen(), "clearUnseen reports a change when the count was above 0");
            check ((int) badge.toJson()["unseenLanded"] == 0, "clearUnseen zeroes the count");
            check (! badge.clearUnseen(), "a second clearUnseen reports no change");
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
        line ("acquire-probe install  <zip> <id> <libraryRoot> [displayName]");
        line ("acquire-probe fetch    <base> <id> <file> <ticket> <libraryRoot>");
        line ("acquire-probe selftest <scratchDir>");
        line ("acquire-probe scan");
        return 2;
    }

    const auto command = args[0];

    // The library exactly as the plugin sees it, with no DAW and no webview.
    //
    // This exists because listing and loading were allowed to disagree: scan()
    // tested for instrument.sfz while resolve() accepted a Faust folder too, so
    // a Faust instrument loaded perfectly and never appeared in the list. There
    // was no way to see that without opening a DAW, which is exactly the kind
    // of thing this tool tree exists to make visible.
    if (command == "scan")
    {
        const InstrumentLibrary library;
        const auto entries = library.scan();

        line ("root: " + InstrumentLibrary::root().getFullPathName());
        line (juce::String (entries.size()) + " instrument(s)");

        for (const auto& e : entries)
            line ("  " + juce::String (e.kind == InstrumentLibrary::Kind::faust
                                           ? "faust  " : "sampler")
                  + "  " + e.id.paddedRight (' ', 40)
                  + "  \"" + e.name + "\"");

        return 0;
    }

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
            return fail ("install needs <zip> <id> <libraryRoot> [displayName]");

        // The display name is optional here and always present in practice:
        // the site puts it in the sentinel, so fetchAndInstall never omits it.
        // This command did omit it, which meant the sidecar MERGE -- the
        // server's provenance and measurements surviving the plugin's name
        // stamp -- could only be exercised by the selftest against a synthetic
        // fixture, never against an archive a real server produced. An
        // end-to-end run reported the name check failing when the truth was
        // that this command had silently ignored the argument.
        const auto r = installFromArchive (juce::File (args[1]), args[2], juce::File (args[3]),
                                           args.size() >= 5 ? args[4] : juce::String());
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
