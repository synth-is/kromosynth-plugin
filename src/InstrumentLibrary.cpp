#include "InstrumentLibrary.h"

juce::File InstrumentLibrary::root()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("Synth.is")
               .getChildFile ("Instruments");
}

juce::String InstrumentLibrary::readSidecarName (const juce::File& folder)
{
    const auto sidecar = folder.getChildFile ("synthis.json");

    if (! sidecar.existsAsFile())
        return {};

    // Fails quietly to empty. A malformed sidecar should cost the instrument
    // its nice label, not its place in the list -- the SFZ is the thing that
    // makes it playable, and this file is decoration on top of it.
    const auto parsed = juce::JSON::parse (sidecar.loadFileAsString());

    if (auto* obj = parsed.getDynamicObject())
        return obj->getProperty ("name").toString().trim();

    return {};
}

juce::String InstrumentLibrary::readDisplayName (const juce::File& sfz,
                                                 const juce::String& fallbackId)
{
    // Only the header. Reading the whole file to find a comment would mean
    // opening every SFZ on every scan, and these run to tens of kilobytes.
    juce::FileInputStream stream (sfz);

    if (! stream.openedOk())
        return fallbackId;

    const auto header = stream.readString().substring (0, 2048);

    for (const auto& line : juce::StringArray::fromLines (header))
    {
        const auto trimmed = line.trim();

        if (! trimmed.startsWith ("//"))
            continue;

        const auto body = trimmed.substring (2).trim();

        // Both spellings. kromosynth-vi writes "Sounds:" from the standard VI
        // generator and "Sound:" from the XY morph one -- found by reading two
        // artefacts after the first version, which matched only the plural,
        // silently fell back to the directory name for every XY instrument.
        // Generalising from one example, again.
        if (body.startsWithIgnoreCase ("Sounds:") || body.startsWithIgnoreCase ("Sound:"))
        {
            const auto value = body.fromFirstOccurrenceOf (":", false, false).trim();

            if (value.isNotEmpty())
                return value;
        }
    }

    return fallbackId;
}

std::vector<InstrumentLibrary::Entry> InstrumentLibrary::scan() const
{
    std::vector<Entry> entries;

    const auto base = root();

    if (! base.isDirectory())
        return entries;

    for (const auto& dir : juce::RangedDirectoryIterator (base, false, "*",
                                                          juce::File::findDirectories))
    {
        const auto folder = dir.getFile();
        const auto sfz = folder.getChildFile ("instrument.sfz");

        // Silently skipping a directory without an instrument.sfz is right --
        // a samples folder or a partial download is not an error -- but note
        // this is the one place the library can be quietly wrong. A download
        // that lands samples and dies before writing the SFZ leaves a
        // directory that simply does not appear. The acquisition path should
        // write the SFZ last for exactly this reason.
        if (! sfz.existsAsFile())
            continue;

        Entry entry;
        entry.id = folder.getFileName();
        entry.sfz = sfz;
        // The sidecar wins when present. It holds the label the site showed on
        // the card the user clicked, which is the only name they have ever seen
        // for this instrument. The SFZ header's "Sounds:" ULID is a fallback for
        // instruments that predate acquisition or were placed here by hand.
        entry.name = readSidecarName (folder);

        if (entry.name.isEmpty())
            entry.name = readDisplayName (sfz, entry.id);

        entries.push_back (std::move (entry));
    }

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b)
               {
                   return a.name.compareIgnoreCase (b.name) < 0;
               });

    return entries;
}

InstrumentLibrary::Resolved InstrumentLibrary::resolve (const juce::String& id) const
{
    // Rejects anything that could escape the library root. `id` arrives from
    // JavaScript, so "../../../etc/passwd" is a string someone can send, and
    // File::getChildFile would resolve it happily.
    //
    // This is the ONLY place that check lives. pathFor delegates here rather
    // than repeating it, because a second copy is a second thing to forget to
    // update.
    if (id.isEmpty() || id.containsChar ('/') || id.containsChar ('\\')
        || id.contains (".."))
        return {};

    const auto folder = root().getChildFile (id);

    const auto sfz = folder.getChildFile ("instrument.sfz");
    if (sfz.existsAsFile())
        return { Kind::sampler, sfz, {} };

    const auto wasm = folder.getChildFile ("instrument.wasm");
    const auto json = folder.getChildFile ("instrument.json");

    // BOTH are required. A .wasm without its .json is not a degraded
    // instrument, it is an unplayable one: the struct size and the parameter
    // byte offsets live in the JSON and cannot be recovered from the module.
    if (wasm.existsAsFile() && json.existsAsFile())
        return { Kind::faust, wasm, json };

    return {};
}

juce::File InstrumentLibrary::pathFor (const juce::String& id) const
{
    const auto r = resolve (id);
    return r.kind == Kind::sampler ? r.primary : juce::File{};
}
