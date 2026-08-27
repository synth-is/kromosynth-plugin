// ─────────────────────────────────────────────────────────────────────────────
// sfz-probe — what does sfizz actually do with our SFZ?
//
// KEEPABLE, but not shipped. This is a developer tool: it links sfizz and
// nothing else, has no JUCE, no audio device, no DAW and no UI. It exists
// because every open question in STAGE0_FINDINGS.md section E8 is about what
// sfizz makes of a file we generated, and none of those questions need a
// plugin to answer.
//
// Two subcommands:
//
//   dump   <file.sfz>            load-time conformance. What survived parsing.
//   render <file.sfz> [options]  offline render. What it sounds like, measured.
//
// The SFZ path is argv, NOT a compile-time define. The whole point of the dump
// is comparing two artefacts, and a define would make that two reconfigures.
// (The plugin engine, when it exists, takes the define; different problem.)
//
// Output is one fact per line, stable field order, so two dumps diff cleanly.
// Nothing is printed that was not read back out of sfizz.
//
// IMPORTANT — redirect stderr separately when running this. sfizz writes parse
// errors and warnings to std::cerr from Synth::Impl::onParseError and
// onParseWarning, and in a Debug build its DBG() macro is live and also goes to
// stderr. That second stream is not decoration: opcodes rejected in <control>
// scope appear ONLY there. See the note on getUnknownOpcodes below.
// ─────────────────────────────────────────────────────────────────────────────

#include <sfizz.hpp>
#include <sfizz_message.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Reading state back out of sfizz.
//
// sfizz 1.2.3 exposes 255 OSC-style introspection paths (SynthMessaging.cpp)
// through createClient/setReceiveCallback/sendMessage in the public header.
// dispatchMessage calls the receive callback INLINE, so a query is synchronous
// and the reply is available the moment sendMessage returns.
//
// This is a far stronger diagnostic than getUnknownOpcodes(), which:
//   - deduplicates by opcode name across the whole file, so it cannot say how
//     many times or where a name was rejected;
//   - is fed only from Synth::Impl::buildRegion, so <control> opcodes never
//     reach it at all (handleControlOpcodes sends unknowns to DBG and nothing
//     else) - absence from the list proves nothing about set_ccN;
//   - is populated when Region::parseOpcode returns false, so "accepted" is not
//     the same as "has an effect".
//
// Both are reported. The unknown list is the cheap signal; the introspection is
// the one to believe.
//
// Thread-safety note, recorded so it is not rediscovered in the plugin:
// sfizz.hpp documents sendMessage as RT, i.e. it must be called from the same
// task as renderBlock. This tool is single threaded and never renders while
// querying, so there is no race here. That does NOT make it safe to call from
// a plugin's message thread.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct Reply {
    bool received = false;
    std::string sig;
    std::vector<std::string> values;

    // The numeric value alongside the text, for every numeric tag.
    //
    // This exists because the first version of this tool did NOT have it, and
    // reconstructed MIDI velocities by parsing its own printed output. At
    // "%g" that is six significant digits: 43/127 printed as 0.338583 and read
    // back as 0.338583, which times 127 is 43.000041, which ceils to 44. The
    // velocity bands came out as 1-43 and 43-84, overlapping at 43, and the
    // overlap was an artefact of the formatter rather than anything sfizz did.
    // Never round-trip a measurement through its own display string.
    std::vector<double> numbers;

    bool isNull() const { return !received || sig == "N" || sig.empty(); }
    const std::string& value(size_t i = 0) const
    {
        static const std::string empty;
        return i < values.size() ? values[i] : empty;
    }
    double number(size_t i = 0) const
    {
        return i < numbers.size() ? numbers[i] : 0.0;
    }
};

// 9 significant digits round-trips a float exactly. Slightly uglier to read
// than %g and worth it: these lines get diffed between artefacts.
std::string formatFloat(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

void receiveReply(void* data, int /*delay*/, const char* /*path*/,
                  const char* sig, const sfizz_arg_t* args)
{
    Reply& reply = *static_cast<Reply*>(data);
    reply.received = true;
    reply.sig = sig ? sig : "";
    reply.values.clear();
    reply.numbers.clear();

    if (!sig)
        return;

    for (size_t i = 0; sig[i] != '\0'; ++i) {
        // The void-valued OSC tags still consume an argument slot in sfizz's
        // Client::receive template (OSC_VOID_TRAITS zero-fills one), so the
        // index stays in step with the signature either way.
        const sfizz_arg_t* a = args ? &args[i] : nullptr;

        switch (sig[i]) {
        case 'i': case 'c': case 'r':
            reply.values.push_back(a ? std::to_string(a->i) : "?");
            reply.numbers.push_back(a ? double(a->i) : 0.0);
            break;
        case 'h':
            reply.values.push_back(a ? std::to_string(a->h) : "?");
            reply.numbers.push_back(a ? double(a->h) : 0.0);
            break;
        case 'f':
            reply.values.push_back(a ? formatFloat(a->f) : "?");
            reply.numbers.push_back(a ? double(a->f) : 0.0);
            break;
        case 'd':
            reply.values.push_back(a ? formatFloat(a->d) : "?");
            reply.numbers.push_back(a ? a->d : 0.0);
            break;
        case 's': case 'S':
            reply.values.push_back(a && a->s ? std::string(a->s) : std::string());
            break;
        case 'b':
            reply.values.push_back(a && a->b ? ("blob:" + std::to_string(a->b->size))
                                             : std::string("blob:?"));
            break;
        case 'm':
            reply.values.push_back("midi");
            break;
        case 'T': reply.values.push_back("true");  break;
        case 'F': reply.values.push_back("false"); break;
        case 'N': reply.values.push_back("null");  break;
        case 'I': reply.values.push_back("inf");   break;
        default:
            reply.values.push_back(std::string("?") + sig[i]);
            break;
        }
    }
}

class Probe {
public:
    Probe()
        : client_(sfz::Sfizz::createClient(&reply_))
    {
        sfz::Sfizz::setReceiveCallback(*client_, &receiveReply);
    }

    sfz::Sfizz& synth() { return synth_; }

    // Returns a reference to internal state; valid only until the next query.
    const Reply& query(const std::string& path)
    {
        reply_ = Reply{};
        synth_.sendMessage(*client_, 0, path.c_str(), "", nullptr);
        return reply_;
    }

private:
    sfz::Sfizz synth_;
    Reply reply_;
    sfz::Sfizz::ClientPtr client_;
};

std::string regionPath(int region, const char* leaf)
{
    return "/region" + std::to_string(region) + "/" + leaf;
}

std::string regionCcPath(int region, const char* leaf, int cc)
{
    return "/region" + std::to_string(region) + "/" + leaf + std::to_string(cc);
}

// Query a path and print "key value" only when sfizz returned something other
// than nil. Silence means "sfizz has no such binding", which is the answer we
// want and is not worth a line each for 128 CCs.
void printIfSet(Probe& probe, const std::string& label, const std::string& path)
{
    const Reply& r = probe.query(path);
    if (r.isNull())
        return;

    std::string out = label;
    for (const auto& v : r.values)
        out += " " + v;
    std::printf("%s\n", out.c_str());
}

// Velocity ranges come back normalised, and NOT by a plain divide by 127.
// Default::hiVel carries kFillGap, so hivel=42 is stored as
// nextafter(43/127, 0) - just under 43/127, not 42/127 - which makes adjacent
// bands half-open and therefore non-overlapping. Multiplying getEnd() by 127
// and rounding gives 43, an off-by-one waiting to happen. kFillGap is not
// applied when the input is 127, so a full-range band ends at exactly 1.0.
// Both facts are handled here and the raw floats are printed alongside, so the
// reconstruction can be checked rather than trusted.
int hiVelToMidi(double end)
{
    if (end >= 1.0)
        return 127;
    return static_cast<int>(std::ceil(end * 127.0)) - 1;
}

void printVelRange(Probe& probe, const std::string& label, const std::string& path)
{
    const Reply& r = probe.query(path);
    if (r.isNull() || r.values.size() < 2)
        return;

    // From r.numbers, NOT from r.values. See the comment on Reply::numbers.
    const double lo = r.number(0);
    const double hi = r.number(1);

    std::printf("%s %s %s (midi %d %d)\n",
                label.c_str(), r.value(0).c_str(), r.value(1).c_str(),
                static_cast<int>(lo * 127.0 + 0.5), hiVelToMidi(hi));
}

// Every per-region CC-modulated destination our generator can emit, plus the
// paired curve path. Kept as data so adding a destination is one line.
struct CcDestination {
    const char* name;       // label used in the output
    const char* depthLeaf;  // e.g. "amplitude_cc"
    const char* curveLeaf;  // e.g. "amplitude_curvecc", or nullptr
};

const CcDestination kCcDestinations[] = {
    { "amplitude",      "amplitude_cc",      "amplitude_curvecc"      },
    { "volume",         "volume_cc",         "volume_curvecc"         },
    { "pan",            "pan_cc",            "pan_curvecc"            },
    { "pitch",          "pitch_cc",          "pitch_curvecc"          },
    { "ampeg_attack",   "ampeg_attack_cc",   "ampeg_attack_curvecc"   },
    { "ampeg_decay",    "ampeg_decay_cc",    "ampeg_decay_curvecc"    },
    { "ampeg_sustain",  "ampeg_sustain_cc",  "ampeg_sustain_curvecc"  },
    { "ampeg_release",  "ampeg_release_cc",  "ampeg_release_curvecc"  },
    { "ampeg_delay",    "ampeg_delay_cc",    "ampeg_delay_curvecc"    },
    { "ampeg_hold",     "ampeg_hold_cc",     "ampeg_hold_curvecc"     },
    { "offset",         "offset_cc",         nullptr                  },
    { "end",            "end_cc",            nullptr                  },
};

int runDump(const char* sfzPath)
{
    Probe probe;
    probe.synth().setSampleRate(48000.0f);
    probe.synth().setSamplesPerBlock(512);

    std::printf("# sfz-probe dump\n");
    std::printf("file %s\n", sfzPath);

    const bool loaded = probe.synth().loadSfzFile(sfzPath);
    std::printf("load.ok %s\n", loaded ? "true" : "false");
    if (!loaded) {
        std::printf("# loadSfzFile returned false; see stderr for parse errors\n");
        return 1;
    }

    // ── Counts ───────────────────────────────────────────────────────────────
    const int numRegions = probe.synth().getNumRegions();
    const int numGroups  = probe.synth().getNumGroups();
    const int numMasters = probe.synth().getNumMasters();
    const int numCurves  = probe.synth().getNumCurves();

    std::printf("num_regions %d\n", numRegions);
    std::printf("num_groups %d\n", numGroups);
    std::printf("num_masters %d\n", numMasters);
    std::printf("num_samples_preloaded %d\n",
                static_cast<int>(probe.synth().getNumPreloadedSamples()));

    // num_curves needs its arithmetic stated or it reads as a defect.
    // CurveSet::createPredefined seeds 7 (Curve.h NumPredefinedCurves), and
    // addCurve with an explicit index RESIZES the vector to index+1, leaving
    // any skipped slots null. So three curves declared at 10, 11 and 12 give
    // 13, not 3, and 13 is correct.
    std::printf("num_curves %d\n", numCurves);
    std::printf("# num_curves counts 7 predefined plus every slot up to the\n");
    std::printf("# highest explicit curve_index, including null gaps.\n");
    std::printf("num_curves_above_predefined %d\n", numCurves > 7 ? numCurves - 7 : 0);

    // ── Unknown opcodes ──────────────────────────────────────────────────────
    // Deduplicated by name, region scope only. See the header note.
    const std::vector<std::string>& unknown = probe.synth().getUnknownOpcodes();
    std::printf("unknown_opcodes.count %d\n", static_cast<int>(unknown.size()));
    {
        std::vector<std::string> sorted = unknown;
        std::sort(sorted.begin(), sorted.end());
        for (const auto& name : sorted)
            std::printf("unknown_opcode %s\n", name.c_str());
    }
    std::printf("# unknown_opcodes covers region scope only. <control> opcodes\n");
    std::printf("# that sfizz does not support never appear here - they go to\n");
    std::printf("# DBG() on stderr. Check the stderr capture too.\n");

    // ── Control-scope state, read back rather than assumed ───────────────────
    printIfSet(probe, "root_path", "/root_path");
    printIfSet(probe, "image", "/image");
    printIfSet(probe, "note_offset", "/note_offset");
    printIfSet(probe, "octave_offset", "/octave_offset");

    // CC defaults. This is what settles set_ccN: sfizz stores them in
    // defaultCCValues_ and /ccN/default reads that back, normalised to 0..1.
    // Reported for every CC with a non-zero default, so sfizz's own seeded
    // defaults (7, 10, 11) show up and confirm the reader works.
    for (int cc = 0; cc < 128; ++cc) {
        const Reply& r = probe.query("/cc" + std::to_string(cc) + "/default");
        if (r.isNull())
            continue;
        const double v = r.number();
        if (v == 0.0)
            continue;
        std::printf("cc_default %d %s (x127 = %s)\n",
                    cc, r.value().c_str(), formatFloat(v * 127.0).c_str());
    }

    // ── Per region ───────────────────────────────────────────────────────────
    //
    // Two things in this section are sfizz's doing, not the file's, and both
    // read as findings if you do not know that:
    //
    //   - EVERY region carries amplitude on cc7 and cc11 (curve 4) and pan on
    //     cc10 (curve 1). That is sfizz implementing MIDI volume, expression
    //     and pan. Nothing in the SFZ asked for it. Note the consequence for
    //     absolute level: cc7 defaults to 100/127 and curve 4 is x squared, so
    //     the standing amplitude factor is about 0.62, not 1.0.
    //   - loop_range is reported as the whole sample even when loop_mode is
    //     no_loop. It is the default extent, not evidence of a loop. Read
    //     loop_mode for that.
    for (int i = 0; i < numRegions; ++i) {
        const std::string tag = "region " + std::to_string(i);

        printIfSet(probe, tag + " sample",         regionPath(i, "sample"));
        printIfSet(probe, tag + " enabled",        regionPath(i, "enabled"));
        // NOT the <group> header index. /region&/group returns region.group,
        // which is the SFZ `group=` opcode (the polyphony / off_by group). Our
        // generator never emits it, so this reads 0 for every region even on a
        // file with five <group> headers. Labelled poly_group so nobody reads
        // "group 0" everywhere as the headers having collapsed. The header
        // count is num_groups, above.
        printIfSet(probe, tag + " poly_group",     regionPath(i, "group"));
        printIfSet(probe, tag + " key_range",      regionPath(i, "key_range"));
        printIfSet(probe, tag + " pitch_keycenter",regionPath(i, "pitch_keycenter"));
        printVelRange(probe, tag + " vel_range",   regionPath(i, "vel_range"));
        printIfSet(probe, tag + " seq_length",     regionPath(i, "seq_length"));
        printIfSet(probe, tag + " seq_position",   regionPath(i, "seq_position"));
        printIfSet(probe, tag + " trigger",        regionPath(i, "trigger"));

        // Sample extent and looping. Confirms section D prediction 4 from
        // sfizz's side: no loop_mode in the file and no smpl chunk in the WAV
        // means holding a key past the sample gives silence.
        printIfSet(probe, tag + " loop_mode",      regionPath(i, "loop_mode"));
        printIfSet(probe, tag + " loop_range",     regionPath(i, "loop_range"));
        printIfSet(probe, tag + " count",          regionPath(i, "count"));
        printIfSet(probe, tag + " end",            regionPath(i, "end"));

        // Static amplitude and envelope, as parsed.
        printIfSet(probe, tag + " amplitude",      regionPath(i, "amplitude"));
        printIfSet(probe, tag + " volume",         regionPath(i, "volume"));
        printIfSet(probe, tag + " amp_veltrack",   regionPath(i, "amp_veltrack"));
        printIfSet(probe, tag + " ampeg_attack",   regionPath(i, "ampeg_attack"));
        printIfSet(probe, tag + " ampeg_decay",    regionPath(i, "ampeg_decay"));
        printIfSet(probe, tag + " ampeg_sustain",  regionPath(i, "ampeg_sustain"));
        printIfSet(probe, tag + " ampeg_release",  regionPath(i, "ampeg_release"));

        // Every CC binding sfizz retained, per destination, with its curve.
        // Silence for a CC means no binding, which is the interesting answer
        // when the file asked for one.
        //
        // Two paths behave differently and the difference caught me out:
        //
        //   - amplitude/volume/pan/pitch go through Region::ccModDepth, which
        //     returns absl::nullopt when there is no connection. Nil means
        //     absent, exactly as wanted.
        //   - the ampeg_* / offset / end paths read a CCMap with
        //     getWithDefault, which returns a zero-valued entry for every one
        //     of the 128 CCs. They are NEVER nil. Left unfiltered they emit
        //     roughly 43k lines of nothing for a 42-region file.
        //
        // So a depth of exactly zero with a curve of zero is suppressed. That
        // does also suppress a binding explicitly written with depth 0 - which
        // is a no-op binding, and the suppression is stated here rather than
        // being silent.
        for (const CcDestination& dest : kCcDestinations) {
            for (int cc = 0; cc < 128; ++cc) {
                const Reply& depth = probe.query(regionCcPath(i, dest.depthLeaf, cc));
                if (depth.isNull())
                    continue;

                const std::string depthText = depth.value();
                const bool depthIsZero = (depth.number() == 0.0);

                std::string curveText;
                if (dest.curveLeaf != nullptr) {
                    const Reply& curve = probe.query(regionCcPath(i, dest.curveLeaf, cc));
                    curveText = curve.isNull() ? std::string("none") : curve.value();
                }

                const bool curveIsDefault =
                    curveText.empty() || curveText == "0" || curveText == "none";

                if (depthIsZero && curveIsDefault)
                    continue;

                std::string line = tag + " cc" + std::to_string(cc) + " "
                                 + dest.name + " depth=" + depthText;
                if (!curveText.empty())
                    line += " curve=" + curveText;

                std::printf("%s\n", line.c_str());
            }
        }
    }

    std::printf("# end\n");
    return 0;
}

int usage()
{
    std::fprintf(stderr,
        "usage:\n"
        "  sfz-probe dump <file.sfz>\n"
        "\n"
        "Redirect stderr separately - sfizz reports parse errors and\n"
        "unsupported <control> opcodes there, not on stdout.\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
        return usage();

    const std::string command = argv[1];

    if (command == "dump")
        return runDump(argv[2]);

    return usage();
}
