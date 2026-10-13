// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
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
//   curve  <file.sfz> <cc>       sweep one CC and trace the gain curve it maps.
//
// The render subcommands need no audio device and no MIDI hardware. noteOn,
// cc and renderBlock are plain calls, and enableFreeWheeling() makes the file
// pool load synchronously (Synth.cpp:1115), so output is deterministic.
//
// EVERY reported level is accompanied by a peak. EXPERIMENT_01_DSP_EXECUTION.md
// section 4.2 puts it well: a runtime that renders silence is infinitely fast.
// The same trap sits here - see F11 in STAGE0_FINDINGS.md, where loadSfzFile
// returns true after every region was culled for a missing sample. A number
// taken from an all-zero buffer is not a measurement.
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
#include <cstdint>
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

// ─────────────────────────────────────────────────────────────────────────────
// Offline render
// ─────────────────────────────────────────────────────────────────────────────

struct RenderOptions {
    int note = 60;
    int velocity = 100;
    double seconds = 1.5;
    int repeat = 1;
    std::vector<std::pair<int, int>> ccs;

    // Isolation: synthesise a one-region SFZ instead of using the file's own.
    // Needed because the XY instrument sounds five layers at once, so a level
    // measured on the whole file cannot attribute gain to one binding.
    std::string isolateSample;
    int isoCc = -1;
    int isoCc2 = -1;
    int isoCurve = 0;
    int isoCurve2 = 0;
    bool isoCopyCurves = false;
};

struct RenderResult {
    double peak = 0.0;
    double rms = 0.0;
    int maxActiveVoices = 0;
    uint64_t fingerprint = 0;

    // Crest factor, rms/peak. Scale-invariant by construction: a pure gain
    // change leaves it untouched, a different sample changes it.
    //
    // This replaced a peak-normalised fingerprint, which did not work. Dividing
    // by the peak and then quantising at 1e-6 amplifies float rounding, badly
    // at low levels - renders of the SAME sample at velocity 10 and velocity 25
    // produced different normalised hashes purely from rounding. A hash needs
    // exact equality and normalisation cannot deliver it; a ratio compared to a
    // tolerance can.
    double crest = 0.0;

    int voicesAtEnd = 0;
};

// A cheap order-sensitive hash of the rendered samples, used to answer "is this
// render the same audio as that one" without needing to know which sample file
// played. Quantised to 1e-6 so that bit-level denormal noise does not make two
// identical renders look different.
uint64_t fingerprintOf(const std::vector<float>& left, double scale)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    for (float v : left) {
        const int64_t q = static_cast<int64_t>(double(v) * scale * 1000000.0);
        h ^= static_cast<uint64_t>(q);
        h *= 1099511628211ull;
    }
    return h;
}

// Keep rendering until every voice has finished, so the next note starts from
// a clean synth. Without this, pass N carries the release tail of pass N-1 and
// every fingerprint after the first is contaminated by its predecessor - which
// is exactly how the first round-robin run produced a bogus ABBBBB.
// Capped so a stuck voice cannot hang the tool.
int settleVoices(sfz::Sfizz& synth, int blockSize)
{
    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);
    float* buffers[2] = { left.data(), right.data() };

    const int maxBlocks = 2000; // ~21 s at 512/48k
    for (int b = 0; b < maxBlocks; ++b) {
        if (synth.getNumActiveVoices() == 0)
            return b;
        synth.renderBlock(buffers, static_cast<size_t>(blockSize), 1);
    }
    return -1; // did not settle
}

// One note-on, hold, note-off, tail. Returns level and fingerprint.
RenderResult renderOneNote(sfz::Sfizz& synth, const RenderOptions& opt,
                           int blockSize, double sampleRate)
{
    const size_t totalFrames =
        static_cast<size_t>(opt.seconds * sampleRate);

    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);
    float* buffers[2] = { left.data(), right.data() };

    std::vector<float> capturedLeft;
    capturedLeft.reserve(totalFrames);

    RenderResult result;
    double sumSquares = 0.0;
    size_t rendered = 0;

    // Note-off at 70% so the release tail is inside the window rather than
    // truncated by it.
    const size_t noteOffFrame = static_cast<size_t>(totalFrames * 0.7);

    synth.noteOn(0, opt.note, opt.velocity);
    bool released = false;

    while (rendered < totalFrames) {
        if (!released && rendered >= noteOffFrame) {
            synth.noteOff(0, opt.note, 0);
            released = true;
        }

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        synth.renderBlock(buffers, static_cast<size_t>(blockSize), 1);

        result.maxActiveVoices =
            std::max(result.maxActiveVoices, synth.getNumActiveVoices());

        for (int i = 0; i < blockSize && rendered < totalFrames; ++i, ++rendered) {
            const double l = left[static_cast<size_t>(i)];
            const double r = right[static_cast<size_t>(i)];
            result.peak = std::max(result.peak, std::max(std::fabs(l), std::fabs(r)));
            sumSquares += l * l + r * r;
            capturedLeft.push_back(left[static_cast<size_t>(i)]);
        }
    }

    result.rms = std::sqrt(sumSquares / double(rendered * 2));
    result.fingerprint = fingerprintOf(capturedLeft, 1.0);
    result.crest = result.peak > 0.0 ? result.rms / result.peak : 0.0;
    result.voicesAtEnd = synth.getNumActiveVoices();
    return result;
}

// Build a minimal one-region SFZ in memory. loadSfzString resolves relative
// sample paths against the parent directory of the path argument
// (Parser.cpp:71-74 -> FilePool::setRootDirectory), so passing the real
// instrument path makes `samples/foo.wav` resolve exactly as it would in the
// original file. No committed SFZ with machine-specific absolute paths.
std::string buildIsolatedSfz(const RenderOptions& opt,
                             const std::string& curveBlocks)
{
    std::string sfz;
    sfz += "<control>\ndefault_path=samples/\n";

    // Neutralise sfizz's own MIDI volume/expression so the only thing moving
    // the level is what we asked about. cc7 defaults to 100/127 through a
    // squared curve, which is a standing factor of about 0.62 (F8.3).
    sfz += "set_cc7=127\nset_cc11=127\n";

    if (opt.isoCc >= 0)
        sfz += "set_cc" + std::to_string(opt.isoCc) + "=0\n";
    if (opt.isoCc2 >= 0)
        sfz += "set_cc" + std::to_string(opt.isoCc2) + "=0\n";

    sfz += curveBlocks;

    // No envelope. ampeg would smear the level being measured across the
    // window; a flat gain is what makes peak comparable between renders.
    sfz += "\n<region>\n";
    sfz += "sample=" + opt.isolateSample + "\n";
    sfz += "lokey=0 hikey=127 pitch_keycenter=" + std::to_string(opt.note) + "\n";
    sfz += "lovel=1 hivel=127\n";
    sfz += "ampeg_attack=0 ampeg_decay=0 ampeg_sustain=100 ampeg_release=0.001\n";
    sfz += "amp_veltrack=0\n";

    if (opt.isoCc >= 0) {
        sfz += "amplitude_oncc" + std::to_string(opt.isoCc) + "=100\n";
        sfz += "amplitude_curvecc" + std::to_string(opt.isoCc) + "="
             + std::to_string(opt.isoCurve) + "\n";
    }
    if (opt.isoCc2 >= 0) {
        sfz += "amplitude_oncc" + std::to_string(opt.isoCc2) + "=100\n";
        sfz += "amplitude_curvecc" + std::to_string(opt.isoCc2) + "="
             + std::to_string(opt.isoCurve2) + "\n";
    }

    return sfz;
}

// Lift the <curve> blocks verbatim out of the source file, so a curve test
// exercises the artefact's real curve definitions rather than a retyped copy
// of them. Retyping would test the transcription, not the file.
std::string extractCurveBlocks(const char* sfzPath)
{
    std::FILE* f = std::fopen(sfzPath, "rb");
    if (!f)
        return {};

    std::string out;
    char line[1024];
    bool inCurve = false;

    while (std::fgets(line, sizeof(line), f) != nullptr) {
        std::string s(line);
        const bool startsCurve = s.find("<curve>") != std::string::npos;
        const bool startsOther =
            s.find('<') != std::string::npos && !startsCurve;

        if (startsCurve) {
            inCurve = true;
            out += s;
            continue;
        }
        if (inCurve && startsOther) {
            inCurve = false;
            continue;
        }
        if (inCurve)
            out += s;
    }

    std::fclose(f);
    return out;
}

// Load either the real file or a synthesised isolation of it. Returns false if
// nothing playable survived - see F11: loadSfzFile returns true even when every
// region was culled for a missing sample.
bool loadForRender(Probe& probe, const char* sfzPath, const RenderOptions& opt)
{
    bool ok;

    if (opt.isolateSample.empty()) {
        ok = probe.synth().loadSfzFile(sfzPath);
    } else {
        const std::string curves =
            opt.isoCopyCurves ? extractCurveBlocks(sfzPath) : std::string();
        ok = probe.synth().loadSfzString(sfzPath, buildIsolatedSfz(opt, curves));
    }

    if (!ok) {
        std::printf("load.ok false\n");
        return false;
    }

    const int regions = probe.synth().getNumRegions();
    std::printf("load.ok true\nnum_regions %d\n", regions);

    if (regions == 0) {
        std::printf("# ABORT: zero regions survived. loadSfzFile/String returns\n"
                    "# true even when every region was culled for an\n"
                    "# unresolvable sample (F11). Check stderr.\n");
        return false;
    }
    return true;
}

void applyCcs(sfz::Sfizz& synth, const RenderOptions& opt)
{
    for (const auto& cc : opt.ccs)
        synth.cc(0, cc.first, cc.second);
}

void prepare(Probe& probe, int blockSize, double sampleRate)
{
    probe.synth().setSampleRate(static_cast<float>(sampleRate));
    probe.synth().setSamplesPerBlock(blockSize);
    // Makes the file pool load synchronously so the first block is not silent
    // because a background thread had not finished. Without this the first
    // render of a cold instrument measures the loader, not the instrument.
    probe.synth().enableFreeWheeling();
}

int runRender(const char* sfzPath, const RenderOptions& opt)
{
    const int blockSize = 512;
    const double sampleRate = 48000.0;

    Probe probe;
    prepare(probe, blockSize, sampleRate);

    std::printf("# sfz-probe render\nfile %s\n", sfzPath);
    if (!opt.isolateSample.empty())
        std::printf("isolated_sample %s\n", opt.isolateSample.c_str());

    if (!loadForRender(probe, sfzPath, opt))
        return 1;

    std::printf("note %d velocity %d duration_s %g repeats %d\n",
                opt.note, opt.velocity, opt.seconds, opt.repeat);
    for (const auto& cc : opt.ccs)
        std::printf("cc %d = %d\n", cc.first, cc.second);

    applyCcs(probe.synth(), opt);

    std::vector<uint64_t> prints;
    for (int k = 0; k < opt.repeat; ++k) {
        const RenderResult r = renderOneNote(probe.synth(), opt, blockSize, sampleRate);
        prints.push_back(r.fingerprint);

        // Drain before the next pass. Reported, because a pass that did not
        // start from silence has a fingerprint contaminated by its
        // predecessor and must not be compared with the others.
        const int settleBlocks = settleVoices(probe.synth(), blockSize);

        std::printf("pass %d peak %.6f rms %.6f crest %.6f max_voices %d "
                    "fp %016llx voices_at_window_end %d settle_blocks %d%s\n",
                    k + 1, r.peak, r.rms, r.crest, r.maxActiveVoices,
                    static_cast<unsigned long long>(r.fingerprint),
                    r.voicesAtEnd, settleBlocks,
                    r.peak == 0.0 ? "   <-- SILENT, levels meaningless" : "");
    }

    // Fingerprint pattern. This is what answers round-robin cycling without
    // needing to know which sample file each pass played: distinct prints mean
    // distinct audio.
    if (opt.repeat > 1) {
        std::vector<uint64_t> distinct = prints;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());

        std::string pattern;
        for (uint64_t p : prints) {
            const size_t idx = static_cast<size_t>(
                std::find(distinct.begin(), distinct.end(), p) - distinct.begin());
            pattern += static_cast<char>('A' + (idx % 26));
        }
        std::printf("distinct_renders %d of %d\n",
                    static_cast<int>(distinct.size()), opt.repeat);
        std::printf("pattern %s\n", pattern.c_str());
    }

    std::printf("# end\n");
    return 0;
}

// Sweep one CC across its full range and report the level at each step. With
// --isolate this traces the actual gain curve sfizz applies, which is the one
// thing the load-time dump structurally cannot see (F6.2, F6.3).
int runCurve(const char* sfzPath, int cc, const RenderOptions& optIn)
{
    const int blockSize = 512;
    const double sampleRate = 48000.0;

    RenderOptions opt = optIn;
    // Short window: a curve sweep is 17 renders and only the level matters.
    if (opt.seconds > 0.5)
        opt.seconds = 0.4;

    Probe probe;
    prepare(probe, blockSize, sampleRate);

    std::printf("# sfz-probe curve\nfile %s\nswept_cc %d\n", sfzPath, cc);
    if (!opt.isolateSample.empty())
        std::printf("isolated_sample %s iso_cc %d iso_curve %d\n",
                    opt.isolateSample.c_str(), opt.isoCc, opt.isoCurve);

    if (!loadForRender(probe, sfzPath, opt))
        return 1;

    // Reference: the SAME region with NO cc binding at all. That gives the
    // sample's unmodulated level, so every sweep point below is the ABSOLUTE
    // gain the curve applies, directly comparable with the vNNN values written
    // in the file.
    //
    // The first version referenced against cc=127 instead, which is silent for
    // a tent curve - the abort guard caught it rather than reporting a column
    // of 0/0. Referencing a sweep against a point inside the sweep only works
    // if you already know the answer.
    RenderOptions unmod = opt;
    unmod.isoCc = -1;
    unmod.isoCc2 = -1;

    if (!loadForRender(probe, sfzPath, unmod))
        return 1;
    applyCcs(probe.synth(), unmod);
    const RenderResult ref = renderOneNote(probe.synth(), unmod, blockSize, sampleRate);
    settleVoices(probe.synth(), blockSize);

    std::printf("unmodulated_reference peak %.6f rms %.6f\n", ref.peak, ref.rms);
    if (ref.peak == 0.0) {
        std::printf("# ABORT: the unmodulated reference is silent, so every\n"
                    "# gain below would be 0/0. A runtime that renders silence\n"
                    "# is infinitely fast; do not read the numbers.\n");
        return 1;
    }

    // Now reload WITH the binding and sweep.
    if (!loadForRender(probe, sfzPath, opt))
        return 1;

    std::printf("# cc_value  peak      gain_vs_unmodulated\n");
    bool anyAudible = false;
    for (int v = 0; v <= 127; v += 8) {
        applyCcs(probe.synth(), opt);
        probe.synth().cc(0, cc, v);
        const RenderResult r = renderOneNote(probe.synth(), opt, blockSize, sampleRate);
        settleVoices(probe.synth(), blockSize);
        anyAudible = anyAudible || (r.peak > 0.0);
        std::printf("sweep %3d  %.6f  %.4f\n", v, r.peak, r.peak / ref.peak);
    }
    // 127 is not hit by the step of 8, and it is an endpoint that matters.
    applyCcs(probe.synth(), opt);
    probe.synth().cc(0, cc, 127);
    const RenderResult last = renderOneNote(probe.synth(), opt, blockSize, sampleRate);
    settleVoices(probe.synth(), blockSize);
    std::printf("sweep %3d  %.6f  %.4f\n", 127, last.peak, last.peak / ref.peak);

    if (!anyAudible && last.peak == 0.0)
        std::printf("# WARNING: every point in the sweep was silent.\n");

    std::printf("# end\n");
    return 0;
}

bool parseRenderOptions(int argc, char** argv, int first, RenderOptions& opt)
{
    for (int i = first; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasNext = (i + 1 < argc);

        if (a == "--note" && hasNext)            opt.note = std::atoi(argv[++i]);
        else if (a == "--vel" && hasNext)        opt.velocity = std::atoi(argv[++i]);
        else if (a == "--dur" && hasNext)        opt.seconds = std::atof(argv[++i]);
        else if (a == "--repeat" && hasNext)     opt.repeat = std::atoi(argv[++i]);
        else if (a == "--isolate" && hasNext)    opt.isolateSample = argv[++i];
        else if (a == "--iso-cc" && hasNext)     opt.isoCc = std::atoi(argv[++i]);
        else if (a == "--iso-cc2" && hasNext)    opt.isoCc2 = std::atoi(argv[++i]);
        else if (a == "--iso-curve" && hasNext)  opt.isoCurve = std::atoi(argv[++i]);
        else if (a == "--iso-curve2" && hasNext) opt.isoCurve2 = std::atoi(argv[++i]);
        else if (a == "--iso-copy-curves")       opt.isoCopyCurves = true;
        else if (a == "--cc" && hasNext) {
            const std::string spec = argv[++i];
            const size_t eq = spec.find('=');
            if (eq == std::string::npos)
                return false;
            opt.ccs.emplace_back(std::atoi(spec.substr(0, eq).c_str()),
                                 std::atoi(spec.substr(eq + 1).c_str()));
        }
        else return false;
    }
    return true;
}

int usage()
{
    std::fprintf(stderr,
        "usage:\n"
        "  sfz-probe dump   <file.sfz>\n"
        "  sfz-probe render <file.sfz> [options]\n"
        "  sfz-probe curve  <file.sfz> <cc> [options]\n"
        "\n"
        "render/curve options:\n"
        "  --note N       MIDI note (default 60)\n"
        "  --vel V        MIDI velocity (default 100)\n"
        "  --cc N=V       set a CC before the note. Repeatable.\n"
        "  --dur S        seconds to render (default 1.5)\n"
        "  --repeat K     K successive note-on/off cycles (default 1)\n"
        "  --isolate S    synthesise a one-region SFZ around sample S\n"
        "                 (a filename as reported by `dump`) instead of\n"
        "                 loading the file's own regions\n"
        "  --iso-cc N     bind amplitude_oncc<N> in the isolated region\n"
        "  --iso-cc2 N    bind a SECOND amplitude_oncc<N>, for the\n"
        "                 multiply-versus-sum test\n"
        "  --iso-curve C  curve index for --iso-cc (default 0, linear)\n"
        "  --iso-curve2 C curve index for --iso-cc2\n"
        "  --iso-copy-curves  copy the <curve> blocks out of the source\n"
        "                 file into the synthesised one\n"
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

    if (command == "render") {
        RenderOptions opt;
        if (!parseRenderOptions(argc, argv, 3, opt))
            return usage();
        return runRender(argv[2], opt);
    }

    if (command == "curve") {
        if (argc < 4)
            return usage();
        RenderOptions opt;
        if (!parseRenderOptions(argc, argv, 4, opt))
            return usage();
        return runCurve(argv[2], std::atoi(argv[3]), opt);
    }

    return usage();
}
