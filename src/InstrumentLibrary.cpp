#include "InstrumentLibrary.h"

juce::File InstrumentLibrary::root()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("Synth.is")
               .getChildFile ("Instruments");
}

namespace
{

/** The seven dimensions, in DESCRIPTOR_DIMENSIONS index order. */
const char* const kDescriptorNames[] = {
    "brightness", "noisiness", "rolloff", "energy", "percussive", "richness", "width"
};

/** Reads a nested object, or a void var when the key is absent.

    Chaining through these rather than testing at each level, because the whole
    sidecar is optional and so is every block in it: a file with no
    "character" is the normal case for an unadopted candidate, not an error to
    branch on.
*/
juce::var child (const juce::var& v, const char* key)
{
    if (auto* obj = v.getDynamicObject())
        return obj->getProperty (key);

    return {};
}

bool present (const juce::var& v, const char* key)
{
    auto* obj = v.getDynamicObject();
    return obj != nullptr && obj->hasProperty (key);
}

juce::String text (const juce::var& v, const char* key)
{
    return child (v, key).toString().trim();
}

} // namespace

const char* InstrumentLibrary::descriptorName (int index)
{
    return juce::isPositiveAndBelow (index, kNumDescriptors) ? kDescriptorNames[index] : "";
}

InstrumentLibrary::Metadata InstrumentLibrary::readSidecar (const juce::File& folder)
{
    Metadata m;

    const auto sidecar = folder.getChildFile ("synthis.json");

    if (! sidecar.existsAsFile())
        return m;

    // Fails quietly to empty. A malformed sidecar should cost the instrument
    // its nice label, not its place in the list -- the SFZ or the .wasm is the
    // thing that makes it playable, and this file is decoration on top of it.
    const auto root = juce::JSON::parse (sidecar.loadFileAsString());

    if (root.getDynamicObject() == nullptr)
        return m;

    m.name       = text (root, "name");
    m.acquiredAt = text (root, "acquiredAt");

    const auto source = child (root, "source");

    m.substrate      = text (source, "substrate");
    m.evolutionRunId = text (source, "evolutionRunId");
    m.soundClass     = text (source, "class");

    // Only what tells two renditions of one sound apart. sampleRate and
    // pitchSampling are in the artefact too but do not distinguish anything a
    // person is choosing between.
    if (auto* render = child (source, "render").getDynamicObject())
    {
        m.hasRender = true;
        m.renderDuration = (double) render->getProperty ("duration");
        m.velocityLayers = (int) render->getProperty ("velocityLayers");

        if (auto* range = render->getProperty ("pitchRange").getDynamicObject())
        {
            m.pitchLow  = range->getProperty ("low").toString();
            m.pitchHigh = range->getProperty ("high").toString();
        }
    }
    m.creator        = text (source, "creator");
    m.discoveredAt   = text (source, "discoveredAt");
    m.adoptedAt      = text (source, "adoptedAt");
    m.renderedAt     = text (source, "renderedAt");

    // Only when actually present, so "generation 0" and "no generation
    // recorded" stay distinguishable -- generation 0 is a real seed.
    if (present (source, "generation"))
        m.generation = (int) child (source, "generation");

    // A list, because a multi-sound instrument has several. The first is the
    // primary one (RENDERS_SOUND carries is_primary), and that is the one worth
    // showing beside a single-line list entry.
    if (auto* ids = child (source, "soundIds").getArray())
        if (! ids->isEmpty())
            m.soundId = ids->getFirst().toString().trim();

    const auto character = child (root, "character");

    m.soundType              = text (character, "soundType");
    m.labelledAt             = text (character, "labelledAt");
    m.labelsAreCorpusRelative = (bool) child (character, "labelsAreCorpusRelative");

    const auto labels = child (character, "labels");

    for (int i = 0; i < kNumDescriptors; ++i)
        m.labels[i] = text (labels, kDescriptorNames[i]);

    // ALL SEVEN OR NONE. A vector of the wrong length is refused rather than
    // read as far as it goes: the dimensions are positional, so a six-element
    // array would silently shift `width` into `richness` and every ordering
    // built on it would be wrong while looking fine.
    if (auto* raw = child (character, "descriptors").getArray())
    {
        if (raw->size() == kNumDescriptors)
        {
            for (int i = 0; i < kNumDescriptors; ++i)
                m.descriptors[i] = (float) (double) raw->getUnchecked (i);

            m.hasDescriptors = true;
        }
    }

    const auto measured = child (root, "measured");

    // `trim` is the flag rather than the block being non-void, because a block
    // that arrived without it cannot be used for the one thing the plugin would
    // skip work for, and half-trusting it would mean loading an instrument at
    // an unmeasured level.
    if (present (measured, "trim"))
    {
        m.hasMeasured   = true;
        m.trim          = (float) (double) child (measured, "trim");
        m.trimDb        = (float) (double) child (measured, "trimDb");
        m.peak          = (float) (double) child (measured, "peak");
        m.peakC3        = (float) (double) child (measured, "peakC3");
        m.peakA4        = (float) (double) child (measured, "peakA4");
        m.peakA5        = (float) (double) child (measured, "peakA5");

        // Optional and, today, always absent. Read whole or not at all for the
        // same reason the descriptor vector is: the entries are positional, and
        // a short table would shift every pitch above the gap.
        if (auto* sweep = child (measured, "pitchPeaks").getArray())
        {
            if (sweep->size() >= 2)
            {
                m.pitchPeaks.reserve ((size_t) sweep->size());
                for (const auto& v : *sweep)
                    m.pitchPeaks.push_back ((float) (double) v);
            }
        }
        m.rtf           = (double) child (measured, "rtf");
        m.rtfPlatform   = text (measured, "rtfPlatform");
        m.capAt4x       = (int) child (measured, "capAt4x");
        m.articulation  = text (measured, "articulation");
        m.hasGate       = (bool) child (measured, "hasGate");
    }

    return m;
}

juce::String InstrumentLibrary::readSidecarName (const juce::File& folder)
{
    return readSidecar (folder).name;
}

bool InstrumentLibrary::remove (const juce::String& id) const
{
    // resolve() carries the id validation AND answers whether this is really an
    // instrument, so a caller cannot use this to move an arbitrary directory
    // under the library root to the Trash.
    if (resolve (id).kind == Kind::none)
        return false;

    const auto folder = root().getChildFile (id);

    if (! folder.isDirectory())
        return false;

    // moveToTrash rather than deleteRecursively. The instrument cost an
    // extraction from a monthly quota; the Trash is where a reversible deletion
    // belongs, and it is what makes a single click on a small button in a list
    // an acceptable interaction at all.
    return folder.moveToTrash();
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

        // resolve() rather than a second instrument.sfz test. The listing and
        // the loading MUST agree about what counts as an instrument: when they
        // did not, a Faust folder loaded perfectly and never appeared in the
        // list, so there was no way to ask for it.
        const auto resolved = resolve (folder.getFileName());

        // Silently skipping a directory that resolves to nothing is right --
        // a samples folder or a partial download is not an error -- but note
        // this is the one place the library can be quietly wrong. A download
        // that lands samples and dies before writing the SFZ leaves a
        // directory that simply does not appear. The acquisition path should
        // write the SFZ last for exactly this reason, and a Faust artefact
        // should write instrument.json last for the same one.
        if (resolved.kind == Kind::none)
            continue;

        Entry entry;
        entry.id = folder.getFileName();
        entry.primary = resolved.primary;
        entry.kind = resolved.kind;
        // The sidecar wins when present. It holds the label the site showed on
        // the card the user clicked, which is the only name they have ever seen
        // for this instrument. The SFZ header's "Sounds:" ULID is a fallback for
        // instruments that predate acquisition or were placed here by hand.
        //
        // Read whole rather than name-only: the shell sorts and filters on the
        // rest of it (ARTEFACT_CONTRACT §4), and one read per instrument per
        // scan is the same cost the name alone used to be.
        entry.meta = readSidecar (folder);
        entry.name = entry.meta.name;

        // readDisplayName parses an SFZ header, so it has nothing to say about
        // a Faust module. Falling back to the folder name there rather than
        // handing it a .wasm to misparse.
        if (entry.name.isEmpty())
            entry.name = resolved.kind == Kind::sampler
                       ? readDisplayName (resolved.primary, entry.id)
                       : entry.id;

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
