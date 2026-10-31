// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// wclap-host-probe — synthis-et4.3. Drives KromosynthAudioProcessor with a hosted WCLAP, no host, no
// editor, no audio device. DEVELOPER TOOL, never shipped.
//
//     wclap-host-probe <instrument-id>
//     KROMOSYNTH_PROBE_LIBRARY=<dir holding <instrument-id>/instrument.wclap/module.wasm>
//
// Prints KEY=VALUE lines; exit 0 only if every CHECK_ passed.
#include "../../src/PluginProcessor.h"
#include "../../src/WclapEngineView.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

extern "C" bool wclapProbeSnapshotStart (void* nsView, int nth, const char* pngPath, int* w, int* h);
extern "C" int wclapProbeSnapshotState();

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Render { juce::AudioBuffer<float> audio; double wallSeconds = 0; };

double rms (const juce::AudioBuffer<float>& b, int from, int to)
{
    double s = 0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i) { const double x = b.getSample (c, i); s += x * x; ++n; }
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

// `notes` held for the whole render, all started in block 0, through processBlock's MidiBuffer.
Render play (KromosynthAudioProcessor& p, const std::vector<int>& notes, double seconds)
{
    const int blocks = (int) std::ceil (seconds * kRate / kBlock);
    Render r;
    r.audio.setSize (2, blocks * kBlock);
    r.audio.clear();
    juce::AudioBuffer<float> block (2, kBlock);
    juce::MidiBuffer midi;
    const auto t0 = std::chrono::steady_clock::now();
    double inside = 0;

    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        block.clear();
        const auto a = std::chrono::steady_clock::now();
        p.processBlock (block, midi);
        inside += std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
        for (int c = 0; c < 2; ++c) r.audio.copyFrom (c, b * kBlock, block, c, 0, kBlock);
    }
    (void) t0;
    r.wallSeconds = inside;   // processBlock only: no copying, no clearing
    // release everything so the next render starts from a quiet plugin
    midi.clear();
    for (int c = 0; c < 128; ++c) midi.addEvent (juce::MidiMessage::noteOff (1, c), 0);
    for (int i = 0; i < 200; ++i) { block.clear(); p.processBlock (block, midi); midi.clear(); }
    return r;
}

bool pumpUntilLoaded (KromosynthAudioProcessor& p, int ms)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! p.currentInstrument().ok && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    return p.currentInstrument().ok;
}

std::unique_ptr<KromosynthAudioProcessor> makeProcessor()
{
    auto p = std::make_unique<KromosynthAudioProcessor>();
    p->setPlayConfigDetails (0, 2, kRate, kBlock);
    p->prepareToPlay (kRate, kBlock);
    return p;
}

// A parameter to move so that "state round-trips" is a claim about the PATCH, not about defaults: the
// first visible, writable, non-stepped parameter, set 80 % of the way along its range (20 % if the
// default already sits there).
bool movedParam (WclapEngine& e, juce::String& name)
{
    for (int i = 0, n = e.numParams(); i < n; ++i)
    {
        WclapEngine::ParamInfo info;
        if (! e.paramInfo (i, info) || info.hidden || info.readOnly || info.stepped) continue;
        const double span = info.maxValue - info.minValue;
        if (span <= 0) continue;
        double v = info.minValue + 0.8 * span;
        if (std::abs (v - info.defaultValue) < 0.05 * span) v = info.minValue + 0.2 * span;
        e.queueParam (info.id, v);
        name = info.name;
        return true;
    }
    return false;
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IOLBF, 0);
    if (argc < 2) { std::fprintf (stderr, "usage: wclap-host-probe <instrument-id>\n"); return 2; }

    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String id (argv[1]);
    int rc = 0;
    auto check = [&] (const char* what, bool ok) { std::printf ("%s=%s\n", what, ok ? "PASS" : "FAIL"); if (! ok) rc = 1; };

    // ── 0. the pinned per-call time limit ────────────────────────────────────────────────────────────
    // A decision, not a default: docs/PLUGIN_WCLAP_HOST_SPIKE.md (CPU section) keeps the limit at
    // 60000 ms — a hung guest taking down the DAW is worse than the ~30 % CPU it costs. The env
    // override exists for measurement; with it unset the pin is 60000.
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_WCLAP_TIME_LIMIT_MS", {});
        if (env.isEmpty())
        {
            const auto limit = WclapEngine::timeLimitMs();
            std::printf ("TIME_LIMIT_MS=%u\n", limit);
            check ("CHECK_TIME_LIMIT_MS_60000", limit == 60000);
        }
        else
            std::printf ("TIME_LIMIT_MS_ENV_OVERRIDE=%s\n", env.toRawUTF8());
    }

    // ── 1. routing: the library must pick the wclap substrate from the folder's file list ───────────
    {
        InstrumentLibrary lib;
        const auto r = lib.resolve (id);
        std::printf ("RESOLVED_KIND=%s\n", r.kind == InstrumentLibrary::Kind::wclap ? "wclap" : "other");
        check ("CHECK_ROUTES_TO_WCLAP", r.kind == InstrumentLibrary::Kind::wclap);
        if (r.kind != InstrumentLibrary::Kind::wclap) return 1;
    }

    // ── 2. load, play a note from the processor's MIDI path ──────────────────────────────────────────
    auto a = makeProcessor();
    const auto tLoad = juce::Time::getMillisecondCounterHiRes();
    const auto outcome = a->loadInstrument (id);
    std::printf ("LOAD_OK=%d\nLOAD_MS=%.0f\nLOAD_DETAIL=%s\n", outcome.ok ? 1 : 0,
                 juce::Time::getMillisecondCounterHiRes() - tLoad, outcome.detail.toRawUTF8());
    if (! outcome.ok) return 1;
    auto* eng = a->wclapEngine();
    check ("CHECK_ENGINE_IS_WCLAP", eng != nullptr);
    if (eng == nullptr) return 1;

    // The first note after activate: this is where a missing settle block shows (Six Sines finding).
    const auto first = play (*a, { 60 }, 2.0);
    const double level = rms (first.audio, (int) (0.1 * kRate), (int) (1.5 * kRate));
    std::printf ("RMS_C4=%g\nPEAK=%g\n", level, (double) first.audio.getMagnitude (0, first.audio.getNumSamples()));
    check ("CHECK_PLAYS_RMS_ABOVE_0.05", level > 0.05);

    // ── 3. clap.state: same instance, with a moved parameter so the state is not the default ────────
    juce::MemoryBlock s0, s1, s2;
    eng->saveState (s0);
    juce::String moved;
    const bool didMove = movedParam (*eng, moved);
    std::printf ("MOVED_PARAM=%s\n", didMove ? moved.toRawUTF8() : "(none found)");
    play (*a, {}, 0.2);   // one block carries the queued param event in
    const bool saved = eng->saveState (s1);
    std::printf ("STATE_BYTES_DEFAULT=%zu\nSTATE_BYTES_MOVED=%zu\n", s0.getSize(), s1.getSize());
    check ("CHECK_STATE_CAPTURES_PATCH", saved && didMove && s1 != s0);
    const bool l1 = eng->loadState (s1.getData(), s1.getSize());
    const bool sv2 = eng->saveState (s2);
    check ("CHECK_STATE_ROUNDTRIP_SAME_INSTANCE", l1 && sv2 && s2 == s1);
    const auto afterLoad = play (*a, { 60 }, 2.0);   // first note after loadState: the settle block
    const double lvlAfter = rms (afterLoad.audio, (int) (0.1 * kRate), (int) (1.5 * kRate));
    std::printf ("RMS_AFTER_LOADSTATE=%g\n", lvlAfter);
    check ("CHECK_PLAYS_AFTER_LOADSTATE", lvlAfter > 0.05);

    // ── 4. the session: processor state -> a FRESH processor (a DAW reopening the project) ──────────
    juce::MemoryBlock session;
    a->getStateInformation (session);
    std::printf ("SESSION_BYTES=%zu\n", session.getSize());

    auto b = makeProcessor();
    b->setStateInformation (session.getData(), (int) session.getSize());
    const bool restored = pumpUntilLoaded (*b, 120000);
    check ("CHECK_FRESH_PROCESSOR_LOADED_FROM_SESSION", restored);
    if (restored && b->wclapEngine() != nullptr)
    {
        juce::MemoryBlock s3;
        const bool got = b->wclapEngine()->saveState (s3);
        std::printf ("STATE_BYTES_FRESH=%zu\n", s3.getSize());
        check ("CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE", got && s3 == s1);
        const auto fresh = play (*b, { 60 }, 2.0);
        const double lvlFresh = rms (fresh.audio, (int) (0.1 * kRate), (int) (1.5 * kRate));
        std::printf ("RMS_FRESH_INSTANCE=%g\n", lvlFresh);
        check ("CHECK_PLAYS_FRESH_INSTANCE", lvlFresh > 0.05);
    }
    else
        check ("CHECK_STATE_ROUNDTRIP_FRESH_INSTANCE", false);
    b.reset();

    // ── 6. the engine's OWN page, protocol only (synthis-et4.4). No window, no webview: this proves the
    // DSP side of the page protocol through the bridge — hello -> snapshot, a knob frame reaching the
    // engine and its saved state, host automation reaching the page — and the pure pieces the editor's
    // view uses. What it cannot show (the page drawing, the shim, a real drag) is NOT claimed here.
    if (juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_GUI", "1") != "0")
    {
        struct Frames
        {
            std::mutex m;
            std::vector<std::vector<unsigned char>> v;
            void add (const void* d, size_t n) { std::lock_guard<std::mutex> l (m); v.emplace_back ((const unsigned char*) d, (const unsigned char*) d + n); }
            std::vector<std::vector<unsigned char>> take() { std::lock_guard<std::mutex> l (m); auto o = std::move (v); v.clear(); return o; }
        } frames;
        std::vector<std::vector<unsigned char>> seen;   // everything taken so far, in order
        auto pump = [&] (int ms)
        {
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            juce::AudioBuffer<float> blk (2, kBlock);
            juce::MidiBuffer none;
            while (juce::Time::getMillisecondCounter() < end)
            {
                blk.clear();
                a->processBlock (blk, none);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                for (auto& f : frames.take()) seen.push_back (std::move (f));
            }
        };
        auto firstOfType = [&] (unsigned char type, size_t from) -> long
        {
            for (size_t i = from; i < seen.size(); ++i)
                if (seen[i].size() >= 4 && seen[i][0] == type) return (long) i;
            return -1;
        };
        auto frame = [] (unsigned char type) { return std::vector<unsigned char> { type, 0, 0, 0 }; };
        auto put32 = [] (std::vector<unsigned char>& f, juce::uint32 v) { for (int i = 0; i < 4; ++i) f.push_back ((unsigned char) (v >> (8 * i))); };
        auto putF64 = [] (std::vector<unsigned char>& f, double v) { unsigned char b[8]; std::memcpy (b, &v, 8); f.insert (f.end(), b, b + 8); };
        auto get32 = [] (const std::vector<unsigned char>& f, size_t at) { juce::uint32 v = 0; std::memcpy (&v, f.data() + at, 4); return v; };
        auto getF64 = [] (const std::vector<unsigned char>& f, size_t at) { double v = 0; std::memcpy (&v, f.data() + at, 8); return v; };

        const auto tOpen = juce::Time::getMillisecondCounterHiRes();
        const auto g = eng->guiOpen ([&] (const void* d, size_t n) { frames.add (d, n); });
        std::printf ("GUI_OPEN_MS=%.1f\nGUI_URI=%s\nGUI_SIZE=%ux%u\nGUI_DETAIL=%s\n", juce::Time::getMillisecondCounterHiRes() - tOpen,
                     g.uri.toRawUTF8(), g.width, g.height, g.detail.toRawUTF8());
        check ("CHECK_GUI_OPENS_AS_WEBVIEW", g.ok);
        check ("CHECK_GUI_URI_IS_UI_INDEX", g.uri == "/ui/index.html");
        check ("CHECK_GUI_REPORTS_A_SIZE", g.width > 100 && g.height > 100);

        if (g.ok)
        {
            // page -> engine: hello (protocol version 1). The engine answers with a snapshot.
            auto hello = frame (1); put32 (hello, 1);
            const auto tHello = juce::Time::getMillisecondCounterHiRes();
            check ("CHECK_HELLO_ACCEPTED", eng->guiReceive (hello.data(), hello.size()));
            long snap = -1;
            for (int i = 0; i < 100 && snap < 0; ++i) { pump (50); snap = firstOfType (2, 0); }
            std::printf ("SNAPSHOT_MS=%.0f\n", juce::Time::getMillisecondCounterHiRes() - tHello);
            check ("CHECK_SNAPSHOT_ARRIVES", snap >= 0);

            if (snap >= 0)
            {
                const auto& sn = seen[(size_t) snap];
                const juce::uint32 count = get32 (sn, 4);
                const size_t stateAt = 8 + (size_t) count * 12;
                const juce::uint32 stateSize = sn.size() >= stateAt + 4 ? get32 (sn, stateAt) : 0;
                std::printf ("SNAPSHOT_PARAMS=%u\nSNAPSHOT_STATE_BYTES=%u\nSNAPSHOT_FRAME_BYTES=%zu\n", count, stateSize, sn.size());
                check ("CHECK_SNAPSHOT_HAS_PARAMS_AND_STATE", count > 100 && stateSize > 1000 && sn.size() == stateAt + 4 + stateSize);

                // The knob: the first visible, writable, non-stepped parameter, as a normalised value, from
                // the page. Its clap id is the id the snapshot carries.
                WclapEngine::ParamInfo pi; bool found = false;
                for (int i = 0, n = eng->numParams(); i < n && ! found; ++i)
                {
                    WclapEngine::ParamInfo c;
                    if (eng->paramInfo (i, c) && ! c.hidden && ! c.readOnly && ! c.stepped && c.maxValue > c.minValue) { pi = c; found = true; }
                }
                check ("CHECK_FOUND_A_KNOB", found);
                if (found)
                {
                    double before = 0, snapNorm = -1;
                    eng->paramValue (pi.id, before);
                    for (juce::uint32 k = 0; k < count; ++k)
                        if (get32 (sn, 8 + (size_t) k * 12) == pi.id) snapNorm = getF64 (sn, 8 + (size_t) k * 12 + 4);
                    const double target = snapNorm < 0.5 ? 0.9 : 0.1;
                    std::printf ("KNOB=%s id=%u clap_before=%g snapshot_norm=%g sent_norm=%g\n", pi.name.toRawUTF8(), pi.id, before, snapNorm, target);
                    check ("CHECK_KNOB_IN_SNAPSHOT", snapNorm >= 0);

                    juce::MemoryBlock stBefore, stAfter;
                    eng->saveState (stBefore);
                    auto pf = frame (3); put32 (pf, pi.id); putF64 (pf, target);
                    check ("CHECK_PARAM_FRAME_ACCEPTED", eng->guiReceive (pf.data(), pf.size()));
                    pump (300);
                    double after = 0;
                    eng->paramValue (pi.id, after);
                    eng->saveState (stAfter);
                    const double span = pi.maxValue - pi.minValue;
                    std::printf ("KNOB_CLAP_AFTER=%g (linear expectation %g)\n", after, pi.minValue + target * span);
                    check ("CHECK_KNOB_REACHES_ENGINE", std::abs (after - before) > 1e-6 * span);
                    check ("CHECK_KNOB_REACHES_SAVED_STATE", stAfter != stBefore);

                    // ...and into the processor's session blob, which is what a DAW project keeps.
                    juce::MemoryBlock sessionAfter;
                    a->getStateInformation (sessionAfter);
                    check ("CHECK_KNOB_REACHES_SESSION_BLOB", sessionAfter != session);

                    // engine -> page: host automation of a DIFFERENT parameter comes back as a param frame.
                    WclapEngine::ParamInfo other; bool foundOther = false;
                    for (int i = 0, n = eng->numParams(); i < n && ! foundOther; ++i)
                    {
                        WclapEngine::ParamInfo c;
                        if (eng->paramInfo (i, c) && c.id != pi.id && ! c.hidden && ! c.readOnly && ! c.stepped && c.maxValue > c.minValue)
                        { other = c; foundOther = true; }
                    }
                    if (foundOther)
                    {
                        const auto mark = seen.size();
                        eng->queueParam (other.id, other.minValue + 0.7 * (other.maxValue - other.minValue));
                        bool echoed = false;
                        for (int i = 0; i < 60 && ! echoed; ++i)
                        {
                            pump (50);
                            for (size_t k = mark; k < seen.size(); ++k)
                                if (seen[k].size() >= 16 && seen[k][0] == 3 && get32 (seen[k], 4) == other.id) echoed = true;
                        }
                        std::printf ("AUTOMATED=%s id=%u\n", other.name.toRawUTF8(), other.id);
                        check ("CHECK_AUTOMATION_REACHES_PAGE", echoed);
                    }
                }

                // the editor-size message the page sends when its canvas settles (resize, logical pixels)
                auto rz = frame (7); put32 (rz, g.width); put32 (rz, g.height);
                check ("CHECK_RESIZE_FRAME_ACCEPTED", eng->guiReceive (rz.data(), rz.size()));
            }

            std::printf ("FRAMES_TO_PAGE=%zu\n", seen.size());
            eng->guiClose();
            check ("CHECK_NO_FRAMES_AFTER_CLOSE", ! eng->guiReceive (hello.data(), hello.size()));
        }

        // The view's pure pieces: what the resource provider will and will not serve.
        const auto bundle = eng->bundleDir();
        std::printf ("BUNDLE_DIR=%s\n", bundle.getFullPathName().toRawUTF8());
        const auto idx = WclapEngineView::serve (bundle, "/ui/index.html");
        check ("CHECK_SERVES_INDEX", idx.has_value() && idx->mimeType == "text/html" && ! idx->data.empty());
        check ("CHECK_SERVES_ROOT_AS_INDEX", WclapEngineView::serve (bundle, "/").has_value());
        const auto wasm = WclapEngineView::serve (bundle, "/ui/ui.wasm");
        check ("CHECK_SERVES_UI_WASM", wasm.has_value() && wasm->mimeType == "application/wasm" && wasm->data.size() > 1000000);
        check ("CHECK_SERVES_FACTORY_MANIFEST", WclapEngineView::serve (bundle, "/ui/factory/manifest.json").has_value());
        check ("CHECK_REFUSES_PARENT_TRAVERSAL", ! WclapEngineView::serve (bundle, "/ui/../module.wasm").has_value());
        check ("CHECK_REFUSES_ENCODED_TRAVERSAL", ! WclapEngineView::serve (bundle, "/ui/%2e%2e/module.wasm").has_value());
        check ("CHECK_REFUSES_DSP_MODULE", ! WclapEngineView::serve (bundle, "/module.wasm").has_value());
        check ("CHECK_404_IS_NOTHING", ! WclapEngineView::serve (bundle, "/ui/nope.js").has_value());
        check ("CHECK_SHIM_SETS_EVENT_SOURCE", juce::String (WclapEngineView::shimScript()).contains ("source: window"));
    }

    // ── 6b. OPT-IN (KROMOSYNTH_PROBE_WEBVIEW=<seconds>): the real WebBrowserComponent. Needs a window
    // server session; an unattended ssh/launchd session has none, and then this prints what happened and
    // nothing is claimed. Runs the shim, the resource provider, ui.wasm, the page protocol and the
    // timings of WclapEngineView exactly as the editor builds it.
    if (const int secs = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_WEBVIEW", "0").getIntValue(); secs > 0)
    {
        std::vector<juce::String> logLines;
        auto view = std::make_unique<WclapEngineView> (*eng, [&] (const juce::String& l) { logLines.push_back (l); std::printf ("VIEW_LOG %s\n", l.toRawUTF8()); });
        std::printf ("WEBVIEW_OK=%d WEBVIEW_PROBLEM=%s\n", view->ok() ? 1 : 0, view->problem().toRawUTF8());
        if (view->ok())
        {
            const auto pref = view->preferredSize();
            view->setSize (pref.x, pref.y);
            view->addToDesktop (0);
            view->setVisible (true);
            const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) secs * 1000;
            juce::AudioBuffer<float> blk (2, kBlock);
            juce::MidiBuffer none;
            while (juce::Time::getMillisecondCounter() < end && view->timings().openToReadyMs < 0)
            {
                blk.clear();
                a->processBlock (blk, none);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            }
            auto spin = [&] (int ms)
            {
                const auto e2 = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
                while (juce::Time::getMillisecondCounter() < e2)
                {
                    blk.clear(); a->processBlock (blk, none);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
                }
            };
            spin (5000);   // let the editor settle and paint
            bool evalDone = false;
            view->evaluate ("(() => { const c = document.querySelector('canvas'); return JSON.stringify({canvasCss: c ? c.style.width + 'x' + c.style.height : null,"
                            " dpr: window.devicePixelRatio, inner: innerWidth + 'x' + innerHeight, statusGone: !document.getElementById('status')}); })()",
                            [&] (const juce::String& r) { std::printf ("WEBVIEW_PAGE_STATE=%s\n", r.toRawUTF8()); evalDone = true; });
            for (int i = 0; i < 100 && ! evalDone; ++i) spin (50);
            if (const char* png = std::getenv ("KROMOSYNTH_PROBE_SNAPSHOT"))
            {
                int sw = 0, sh = 0;
                if (auto* peer = view->getPeer(); peer != nullptr && wclapProbeSnapshotStart (peer->getNativeHandle(), 0, png, &sw, &sh))
                {
                    for (int i = 0; i < 200 && wclapProbeSnapshotState() == 1; ++i) spin (50);
                    std::printf ("SNAPSHOT_STATE=%d size=%dx%d file=%s\n", wclapProbeSnapshotState(), sw, sh, png);
                }
                else
                    std::printf ("SNAPSHOT_STATE=no-webview-found\n");
            }

            // A knob moved IN THE REAL PAGE: synthetic pointer events on Surge's canvas, on the Oscillator
            // "Pitch" slider (coordinates read off the first snapshot of this exact build, so this check
            // belongs to this bundle, not to every WCLAP). Page -> shim -> JUCE event -> clap.webview -> guest.
            if (std::getenv ("KROMOSYNTH_PROBE_DRAG") != nullptr)
            {
                juce::MemoryBlock before, afterDrag;
                eng->saveState (before);
                const auto paramsBefore = view->timings().paramFramesFromPage;
                bool started = false;
                view->evaluate ("(() => { const c = document.querySelector('canvas'); c.setPointerCapture = () => {};"
                                " const ev = (t, x, y, b) => c.dispatchEvent(new PointerEvent(t, {clientX: x, clientY: y, buttons: b, pointerId: 1, bubbles: true, cancelable: true}));"
                                " ev('pointermove', 93, 278, 0); ev('pointerdown', 93, 278, 1);"
                                " let i = 1; const t = setInterval(() => { ev('pointermove', 93 + i * 3, 278, 1); if (++i > 20) { clearInterval(t); ev('pointerup', 153, 278, 0); } }, 16);"
                                " return 'started'; })()",
                                [&] (const juce::String&) { started = true; });
                for (int i = 0; i < 100 && ! started; ++i) spin (50);
                spin (2500);
                eng->saveState (afterDrag);
                const auto& t2 = view->timings();
                std::printf ("DRAG_PARAM_FRAMES=%d DRAG_GESTURE_FRAMES=%d\n", t2.paramFramesFromPage - paramsBefore, t2.gestureFramesFromPage);
                check ("CHECK_REAL_PAGE_DRAG_SENDS_PARAM_FRAMES", t2.paramFramesFromPage > paramsBefore);
                check ("CHECK_REAL_PAGE_DRAG_CHANGES_ENGINE_STATE", afterDrag != before);
                juce::MemoryBlock sessionAfterDrag;
                a->getStateInformation (sessionAfterDrag);
                check ("CHECK_REAL_PAGE_DRAG_REACHES_SESSION_BLOB", sessionAfterDrag != session);
                if (const char* png2 = std::getenv ("KROMOSYNTH_PROBE_SNAPSHOT_AFTER"))
                {
                    int sw = 0, sh = 0;
                    if (auto* peer = view->getPeer(); peer != nullptr && wclapProbeSnapshotStart (peer->getNativeHandle(), 0, png2, &sw, &sh))
                    {
                        for (int i = 0; i < 200 && wclapProbeSnapshotState() == 1; ++i) spin (50);
                        std::printf ("SNAPSHOT_AFTER_STATE=%d file=%s\n", wclapProbeSnapshotState(), png2);
                    }
                }
            }
            const auto& t = view->timings();
            std::printf ("WEBVIEW_LOADED_MS=%.0f\nWEBVIEW_READY_MS=%.0f\nWEBVIEW_UI_INIT_MS=%.0f\nWEBVIEW_PAGE=%dx%d\n"
                         "WEBVIEW_RESOURCE_MB=%.1f\nWEBVIEW_RESOURCE_READ_MS=%.0f\nWEBVIEW_FRAMES_TO_PAGE=%d\nWEBVIEW_FRAMES_FROM_PAGE=%d\n",
                         t.openToLoadedMs, t.openToReadyMs, t.uiInitMs, t.pageWidth, t.pageHeight,
                         t.resourceBytes / 1048576.0, t.resourceMs, t.framesToPage, t.framesFromPage);
            check ("CHECK_WEBVIEW_PAGE_READY", t.openToReadyMs >= 0);
        }
        view.reset();
    }

    // ── 6c. OPT-IN (KROMOSYNTH_PROBE_EDITOR=<seconds>): the plugin's REAL editor (shell + engine view),
    // created the way a host does (createEditor), put on the desktop, left to run. Prints the editor size
    // and the wclap_ui_* lines the editor wrote to the processor log, and snapshots every web view in it.
    if (const int secs = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_PROBE_EDITOR", "0").getIntValue(); secs > 0)
    {
        const auto logBefore = a->getLogFile().existsAsFile() ? a->getLogFile().getSize() : 0;
        const auto tEd = juce::Time::getMillisecondCounterHiRes();
        std::unique_ptr<juce::AudioProcessorEditor> ed (a->createEditor());
        const auto createMs = juce::Time::getMillisecondCounterHiRes() - tEd;
        ed->addToDesktop (0);
        ed->setVisible (true);
        juce::AudioBuffer<float> blk (2, kBlock);
        juce::MidiBuffer none;
        auto spin = [&] (int ms)
        {
            const auto e2 = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < e2)
            {
                blk.clear(); a->processBlock (blk, none);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            }
        };
        std::printf ("EDITOR_CREATE_MS=%.0f EDITOR_SIZE_AT_CREATE=%dx%d\n", createMs, ed->getWidth(), ed->getHeight());
        spin (secs * 1000);
        std::printf ("EDITOR_SIZE_AFTER=%dx%d\n", ed->getWidth(), ed->getHeight());
        {   // what the editor logged, wclap lines only
            juce::FileInputStream in (a->getLogFile());
            if (in.openedOk())
            {
                in.setPosition (logBefore);
                for (auto line : juce::StringArray::fromLines (in.readEntireStreamAsString()))
                    if (line.contains ("wclap_ui")) std::printf ("EDITOR_LOG %s\n", line.toRawUTF8());
            }
        }
        if (const char* base = std::getenv ("KROMOSYNTH_PROBE_EDITOR_SNAPSHOT"))
            for (int nth = 0; nth < 3; ++nth)
            {
                int sw = 0, sh = 0;
                const auto path = juce::String (base) + "-" + juce::String (nth) + ".png";
                if (auto* peer = ed->getPeer(); peer != nullptr && wclapProbeSnapshotStart (peer->getNativeHandle(), nth, path.toRawUTF8(), &sw, &sh))
                {
                    for (int i = 0; i < 200 && wclapProbeSnapshotState() == 1; ++i) spin (50);
                    std::printf ("EDITOR_SNAPSHOT nth=%d state=%d view=%dx%d file=%s\n", nth, wclapProbeSnapshotState(), sw, sh, path.toRawUTF8());
                }
            }
        ed.reset();
    }

    // ── 5. CPU: processBlock wall time / audio time, 0 / 1 / 4 / 8 / 16 voices ──────────────────────
    const std::vector<std::vector<int>> chords = {
        {}, { 60 }, { 60, 64, 67, 72 }, { 48, 52, 55, 60, 64, 67, 72, 76 },
        { 36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96 } };
    play (*a, { 60, 64, 67 }, 1.0);   // warm-up, discarded
    for (const auto& chord : chords)
    {
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            const auto r = play (*a, chord, 4.0);
            best = std::min (best, r.wallSeconds / 4.0);
        }
        std::printf ("CPU_RT_FRACTION voices=%zu %g\n", chord.size(), best);
    }

    a.reset();
    return rc;
}
