// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// DISPOSABLE — see PluginEditor.h.

#include "PluginEditor.h"
#include "ShellPage.h"
#include "Palette.h"
#include "CheckoutSentinel.h"
#include "ExternalBrowser.h"
// For renders::outbox(), handed to the runner beside the library root. See
// AcquisitionRunner::start on why both destinations are passed rather than
// resolved in there.
#include "RenderOutbox.h"

using namespace kromosynth;

// For kSpectrogramBins/Frames. PluginProcessor.h only forward-declares
// FaustEngine -- enough to hold a unique_ptr, not enough to read a static
// constant off it.
#if KROMOSYNTH_HAS_FAUST
 #include "FaustEngine.h"
#endif

#include <cstring>

#ifndef KROMOSYNTH_WEB_APP_URL
  #define KROMOSYNTH_WEB_APP_URL "https://synth.is"
#endif
#ifndef KROMOSYNTH_WEBVIEW_PROBE
  #define KROMOSYNTH_WEBVIEW_PROBE 1
#endif
#ifndef KROMOSYNTH_PROBE_DELAY_MS
  #define KROMOSYNTH_PROBE_DELAY_MS 3000
#endif
#ifndef KROMOSYNTH_EDITOR_STUB
  #define KROMOSYNTH_EDITOR_STUB 0
#endif
#ifndef KROMOSYNTH_BROWSER_INSET
  #define KROMOSYNTH_BROWSER_INSET 0
#endif

// ── Where the chrome lives ──────────────────────────────────────────────────
//
// REMOVED 2026-09-11: `kNativeChrome`, the one-build switch that kept the
// native ChromeBar compilable alongside the HTML nav so the two could be
// compared rather than swapped sight-unseen. C36 measured the AUv3 inset at 0,
// which was the condition for keeping the fallback, so both the switch and the
// bar are gone. `git show e393a5f` has the comparison state if it is ever
// wanted again.

// ── Stage 0 environment probe ────────────────────────────────────────────────
//
// DISPOSABLE, and the most disposable thing in the repository — it exists to
// be run four times and then deleted.
//
// The Stage 0 gate in WP §7 asks whether the webview "loads, stays loaded,
// resizes, and does not steal keyboard focus". A webview can pass all four and
// still be useless to us, because the web app depends on browser capabilities
// that WKWebView-inside-a-plugin may not grant. This probe reports the ones
// that are cheap to check and expensive to discover later:
//
//   crossOriginIsolated / SharedArrayBuffer
//       packages/web/vite.config.js sets COEP: credentialless explicitly "for
//       SharedArrayBuffer support" (sqlite-wasm). `credentialless` is a
//       Chromium-first header with late and version-dependent WebKit support.
//       If it is not honoured here, isolation silently fails inside a page
//       that otherwise looks perfect.
//
//   localStorage writability
//       src/services/AuthService.js keeps kromosynth_token / _user / _expiry
//       in localStorage. WKWebView's persistent store lives in the HOSTING
//       APPLICATION's container, so this is per-host, and sandboxed/hardened
//       hosts may deny it outright. Bears directly on WP §5's locally-stored
//       entitlement record.
//
//   AudioContext state
//       Confirms the page has its own audio graph, separate from the plugin's
//       bus. See the comment in PluginProcessor::processBlock.
//
//   WebGL / userAgent
//       three.js is a dependency; and the UA string tells us which WebKit we
//       actually got.
//
// Uncertain API note, stated rather than hidden: this uses
// WebBrowserComponent::evaluateJavascript, which should work on WKWebView
// without native integration enabled. If it turns out to require
// withNativeIntegrationEnabled(true), enable it — on a REMOTE url that only
// injects window.__JUCE__ and does not change the page's origin. It is
// withResourceProvider that changes origin semantics, and that stays off.
// Page-side half of the sentinel rate probe (see pageFinishedLoading). Counts
// everything a cancelled navigation could plausibly disturb, over a fixed
// window, and leaves the answer in window.__sentinelProbeResult for the native
// side to read back. `__HZ__` and `__SECS__` are substituted before injection.
//
// "Nothing happened" has to be a measurement of something, so it also records
// whether the page could scroll at all and whether a field held focus; a run
// on a page that cannot scroll proves nothing about scrolling and says so.
// Native clock for the sentinel rate probe: calls the page's own `fire()` every
// periodMs until the window is spent. Recursion through callAfterDelay with a
// SafePointer rather than a juce::Timer member, so there is nothing to stop and
// nothing left running if the editor is destroyed mid-run.
static void fireSentinelProbe (juce::Component::SafePointer<juce::WebBrowserComponent> browser,
                               int periodMs, int remainingMs)
{
    if (browser == nullptr || remainingMs <= 0)
        return;

    browser->evaluateJavascript ("window.__sentinelFire && window.__sentinelFire()", nullptr);

    juce::Timer::callAfterDelay (periodMs, [browser, periodMs, remainingMs]
    {
        fireSentinelProbe (browser, periodMs, remainingMs - periodMs);
    });
}

static const char* kSentinelProbeScript = R"JS(
(function () {
  if (window.__sentinelProbe) return 'already';
  var HZ = __HZ__, SECS = __SECS__, BURST = __BURST__, DRIVE = '__DRIVE__';
  var se = document.scrollingElement || document.documentElement;
  var field = document.querySelector('input:not([type=hidden]):not([disabled]), textarea');
  if (field) { try { field.focus(); } catch (e) {} }
  var maxScroll = Math.max(0, se.scrollHeight - se.clientHeight);
  if (maxScroll > 0) { window.scrollTo(0, Math.min(300, maxScroll)); }
  var desc = function (el) { return el ? (el.tagName + (el.id ? '#' + el.id : '') + (el.name ? '[' + el.name + ']' : '')) : 'none'; };
  var s = { hz: HZ, secs: SECS, burst: BURST, drive: DRIVE, sent: 0,
            href0: location.href, scrollable: maxScroll, scrollY0: Math.round(window.scrollY),
            hasFocus0: document.hasFocus(), active0: desc(document.activeElement), field: !!field,
            focusEv: 0, blurEv: 0, scrollEv: 0, visEv: 0, pagehide: 0, beforeunload: 0,
            rafFrames: 0, rafOver50: 0, rafOver100: 0, rafMaxMs: 0, hidden0: document.hidden };
  window.addEventListener('focus', function () { s.focusEv++; }, true);
  window.addEventListener('blur', function () { s.blurEv++; }, true);
  window.addEventListener('scroll', function () { s.scrollEv++; }, true);
  document.addEventListener('visibilitychange', function () { s.visEv++; });
  window.addEventListener('pagehide', function () { s.pagehide++; });
  window.addEventListener('beforeunload', function () { s.beforeunload++; });
  var t0 = performance.now(), last = t0, done = false;
  function frame(t) {
    var d = t - last; last = t;
    s.rafFrames++;
    if (d > s.rafMaxMs) s.rafMaxMs = Math.round(d * 10) / 10;
    if (d > 50) s.rafOver50++;
    if (d > 100) s.rafOver100++;
    if (!done) requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
  // BURST sentinels per call, assigned back to back in ONE task: the case a
  // careless caller produces (a phase change and a handover in the same tick),
  // and the one where WebKit could plausibly keep only the last assignment.
  function fire() {
    for (var i = 0; i < BURST; i++) {
      // The draft contract's shape: verb "progress", one job, a phase and a percent.
      location.href = 'synthis://progress?job=probe&phase=rendering&pct=' + (s.sent % 100) + '&n=' + s.sent;
      s.sent++;
    }
  }
  window.__sentinelFire = fire;
  // DRIVE=native leaves the clock to the plugin: a hidden page (a locked screen,
  // a backgrounded window) throttles its own timers to a crawl, which would make
  // a delivery count measure the throttle and not the sentinel.
  var iv = (HZ > 0 && DRIVE === 'page') ? setInterval(fire, 1000 / HZ) : null;
  window.__sentinelProbe = s;
  // An independent 1 s timer, always on, that does nothing but count itself per
  // 10 s bucket and note whether the document was hidden. This is the
  // throttling measurement: a hidden WKWebView slows setInterval, and how much
  // decides how long the plugin can wait for a heartbeat before calling a job
  // lost.
  s.tickBuckets = []; s.hiddenTicks = 0; s.maxTickGapMs = 0;
  var lastTick = performance.now();
  var tickIv = setInterval(function () {
    var now = performance.now(), gap = now - lastTick; lastTick = now;
    if (gap > s.maxTickGapMs) s.maxTickGapMs = Math.round(gap);
    var b = Math.floor((now - t0) / 10000);
    s.tickBuckets[b] = (s.tickBuckets[b] || 0) + 1;
    if (document.hidden) s.hiddenTicks++;
  }, 1000);
  setTimeout(function () {
    done = true;
    clearInterval(tickIv);
    if (iv) clearInterval(iv);
    s.href1 = location.href; s.scrollY1 = Math.round(window.scrollY);
    s.hasFocus1 = document.hasFocus(); s.active1 = desc(document.activeElement);
    s.elapsedMs = Math.round(performance.now() - t0);
    window.__sentinelProbeResult = JSON.stringify(s);
  }, SECS * 1000);
  return 'started';
})()
)JS";

static const char* kProbeScript = R"JS(
(function () {
  var r = {};
  try { r.href = location.href; } catch (e) { r.href = 'ERR ' + e; }
  try { r.ua = navigator.userAgent; } catch (e) { r.ua = 'ERR ' + e; }
  try { r.crossOriginIsolated = String(window.crossOriginIsolated); } catch (e) { r.crossOriginIsolated = 'ERR ' + e; }
  try { r.sharedArrayBuffer = (typeof SharedArrayBuffer); } catch (e) { r.sharedArrayBuffer = 'ERR ' + e; }
  try { r.secureContext = String(window.isSecureContext); } catch (e) { r.secureContext = 'ERR ' + e; }
  try {
    localStorage.setItem('__synthis_probe__', '1');
    r.localStorage = (localStorage.getItem('__synthis_probe__') === '1') ? 'writable' : 'silently-dropped';
    localStorage.removeItem('__synthis_probe__');
  } catch (e) { r.localStorage = 'THREW ' + e; }
  // Persistence across page loads. The intra-session write above proves the API
  // works; it says nothing about whether the store SURVIVES an editor close or
  // a host restart, which is the question §5's auth design actually depends on.
  // Written on first load, reported on every subsequent one, so the answer
  // falls out of the log without needing a JS console.
  try {
    var k = 'synthis_persist_probe';
    var prev = localStorage.getItem(k);
    if (prev === null) {
      localStorage.setItem(k, String(Date.now()));
      r.persist = 'created';
    } else {
      var ageMs = Date.now() - Number(prev);
      r.persist = 'SURVIVED age=' + Math.round(ageMs / 1000) + 's';
    }
  } catch (e) { r.persist = 'THREW ' + e; }
  try {
    var sk = 'synthis_session_probe';
    r.sessionStorage = (sessionStorage.getItem(sk) === null) ? 'fresh' : 'reused';
    sessionStorage.setItem(sk, '1');
  } catch (e) { r.sessionStorage = 'THREW ' + e; }
  // Distinguishes "one plugin instance" from "one plugin WINDOW": each editor
  // gets its own page, so a per-page random id shows whether a second insert
  // created a second webview or reused the first.
  try {
    r.pageId = Math.random().toString(36).slice(2, 8);
  } catch (e) { r.pageId = 'ERR'; }
  try { r.opfs = (navigator.storage && navigator.storage.getDirectory) ? 'present' : 'absent'; } catch (e) { r.opfs = 'ERR ' + e; }
  try {
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) { r.audio = 'no AudioContext'; }
    else { var ac = new AC(); r.audio = 'state=' + ac.state + ' sr=' + ac.sampleRate; ac.close(); }
  } catch (e) { r.audio = 'THREW ' + e; }
  try {
    var c = document.createElement('canvas');
    r.webgl = (c.getContext('webgl2') ? 'webgl2' : (c.getContext('webgl') ? 'webgl1' : 'none'));
  } catch (e) { r.webgl = 'ERR ' + e; }
  try { r.serviceWorker = ('serviceWorker' in navigator) ? 'present' : 'absent'; } catch (e) { r.serviceWorker = 'ERR ' + e; }
  try { r.rootChildren = document.getElementById('root') ? document.getElementById('root').childElementCount : -1; } catch (e) { r.rootChildren = -1; }
  return JSON.stringify(r);
})();
)JS";

KromosynthAudioProcessorEditor::KromosynthAudioProcessorEditor (KromosynthAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc_ (p)
{
    proc_.logEvent ("editor_constructed");

#if KROMOSYNTH_EDITOR_STUB
    // §A3.1c discriminator. Everything below this block — setResizable,
    // setResizeLimits, setSize, resized() logging — is byte-identical to the
    // real build. The ONLY difference is that the editor contains a plain
    // juce::Component instead of a native NSView.
    stub_ = std::make_unique<StubContent>();
    addAndMakeVisible (*stub_);
    proc_.logEvent ("EDITOR_STUB build — no WebBrowserComponent created");
#else
    using Options = juce::WebBrowserComponent::Options;

    auto options = Options{}
        // Without this the page is torn down and reloaded every time the host
        // hides the editor, which would make "does it survive close/reopen"
        // a question about JUCE's default rather than about the host.
        .withKeepPageLoadedWhenBrowserIsHidden()
        // STAGE 1. Exposes window.__JUCE__.backend to this component only.
        // Safe here and nowhere else: the page is served by the resource
        // provider below, from a string compiled into this binary, and
        // pageAboutToLoad refuses to navigate off that origin.
        .withNativeIntegrationEnabled()
        .withResourceProvider ([] (const juce::String& path)
                               -> std::optional<juce::WebBrowserComponent::Resource>
        {
            // The QUERY IS STRIPPED before matching, because the shell is
            // loaded with a cache-busting one appended -- see where goToURL is
            // called. Matching the raw path would 404 the page the moment that
            // query existed.
            const auto clean = path.upToFirstOccurrenceOf ("?", false, false);

            // The logo, and the reason there is a second route here at all.
            //
            // ShellPage.h's rule is no fonts, no CDN and no images, because
            // every external reference is a network request on the one surface
            // justified by working without a network. Handing the SVG over from
            // a compiled-in constant through this same provider KEEPS that
            // rule: the bytes never leave the binary. What the rule forbids is
            // fetching, not drawing.
            if (clean == "/logo.svg")
            {
                const auto* svg = kromosynth::shell::kLogoSvg;
                const auto svgLen = std::strlen (svg);

                std::vector<std::byte> svgBytes (svgLen);
                std::memcpy (svgBytes.data(), svg, svgLen);

                return juce::WebBrowserComponent::Resource {
                    std::move (svgBytes), juce::String ("image/svg+xml") };
            }

            // A DELIVERED RENDER'S BYTES, so the shell page can audition one.
            //
            // This is what makes preview possible without any plugin audio
            // path: the page fetches from its OWN origin and plays it with
            // WebAudio, which reaches the system output exactly as the site's
            // feed play button already does inside the plugin. Nothing enters
            // the host's signal chain, and nothing has to be routed through the
            // processor while a transport may be rolling.
            //
            // A `file://` URL would have been the obvious alternative and does
            // not work: C43 measured that WebKit will not hand a local file to
            // a page on a non-file origin, and it will not load one either.
            // Serving the bytes ourselves sidesteps the whole question.
            //
            // It also keeps ShellPage.h's no-external-references rule, for the
            // same reason /logo.svg does: the rule forbids FETCHING over a
            // network, and this is a local read through the provider.
            if (clean.startsWith ("/render/"))
            {
                // URL-decoded because the page percent-encodes the name and
                // resolve() compares against a real file name. Names carry
                // spaces -- "WCM9MKA 9.0s 4T09A.wav" -- so this is the ordinary
                // case, not an edge one.
                const auto wanted = juce::URL::removeEscapeChars (
                    clean.fromFirstOccurrenceOf ("/render/", false, false));

                const auto f = kromosynth::renders::resolve (wanted);

                if (! f.existsAsFile())
                    return std::nullopt;   // 404 rather than an empty 200

                juce::MemoryBlock mb;

                if (! f.loadFileAsData (mb))
                    return std::nullopt;

                std::vector<std::byte> bytes (mb.getSize());
                std::memcpy (bytes.data(), mb.getData(), mb.getSize());

                return juce::WebBrowserComponent::Resource {
                    std::move (bytes), juce::String ("audio/wav") };
            }

            if (clean != "/" && clean != "/index.html")
                return std::nullopt;

            // The palette substituted in at the ONE place the page is built.
            // Its values live in Palette.h and nowhere else, so this
            // stylesheet and the native chrome cannot drift apart -- see the
            // C26 note in that file for what happens when they can.
            //
            // kStripHeight goes in the same way and for the same reason: the
            // editor lays the shell out at that height when the site is up and
            // the nav#chrome rule draws the bar at it. A literal in both places
            // would be two numbers obliged to agree.
            const juce::String html = juce::String (kromosynth::shell::kIndexHtml)
                                          .replace ("/*PALETTE*/",
                                                    kromosynth::palette::rootCss())
                                          .replace ("/*STRIPH*/",
                                                    juce::String (kromosynth::shell::kStripHeight));
            const auto utf8 = html.toRawUTF8();
            const auto len = std::strlen (utf8);

            std::vector<std::byte> bytes (len);
            std::memcpy (bytes.data(), utf8, len);

            return juce::WebBrowserComponent::Resource {
                // charset=utf-8 EXPLICITLY. The page carries <meta
                // charset="utf-8"> and that is not enough: served from a custom
                // scheme with no charset in the Content-Type, WKWebView decoded
                // it as Latin-1 and a literal U+2026 in a placeholder attribute
                // rendered as "characterâ\u0080¦". Escaped \uXXXX inside the
                // script survived because those are ASCII bytes, which is why
                // only the HTML text showed it.
                std::move (bytes), juce::String ("text/html; charset=utf-8") };
        })
        .withNativeFunction ("engineState",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const auto st = proc_.engineState();
            auto* o = new juce::DynamicObject();
            o->setProperty ("faust", st.faust);
            o->setProperty ("notesOn", st.notesOn);
            o->setProperty ("notesOff", st.notesOff);
            o->setProperty ("activeVoices", st.activeVoices);
            o->setProperty ("forcedStops", st.forcedStops);
            o->setProperty ("trimDb", st.trimDb);
            o->setProperty ("rawPeak", st.rawPeak);
            o->setProperty ("rtf", st.rtf);
            o->setProperty ("voiceCap", st.voiceCap);

            // int64 through juce::var, because blocksRendered is uint64 and a
            // long session would overflow an int. The peak is the field that
            // matters: it is the difference between "rendered nothing audible"
            // and "was never asked to render".
            o->setProperty ("renderPeak", st.renderPeak);
            o->setProperty ("renderBlocks", (juce::int64) st.renderBlocks);

            // The deadline reading. Sent even when no Faust engine is loaded,
            // because processBlock is timed regardless and "nothing loaded and
            // still at 40%" is worth being able to see.
            o->setProperty ("loadRecent", st.loadRecent);
            o->setProperty ("loadMax", st.loadMax);
            o->setProperty ("blocksOver", (juce::int64) st.blocksOver);
            o->setProperty ("budgetMs", st.budgetMs);

            // On the POLL as well as on loadInstrument and getLoadState, and
            // the difference is which loads it covers. Those two are answers to
            // a load the SHELL started, and the shell's load() already says
            // "measuring" for the whole of one. An AUTOLOAD after an
            // acquisition goes through neither -- the completion handler calls
            // loadPendingInstrument directly -- so nothing on this page knew a
            // measurement was running, and refresh() then wrote "Loaded",
            // which is true of the module and false of the sound. C21.
            o->setProperty ("measuring", proc_.isMeasuring());
            done (juce::var (o));
        })
        .withNativeFunction ("panic",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            proc_.panic();
            done (juce::var (true));
        })
        // Diagnostic. Plays one note with no MIDI hardware, which on iOS is the
        // only way to find out whether the audio path reaches the speaker: the
        // standalone app has no on-screen keyboard and no MIDI source unless one
        // is attached. Returns the block counter so the caller can tell "no
        // audio callback" from "callback running, engine silent".
        .withNativeFunction ("testNote",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const int   note = args.size() > 0 ? (int) args[0] : 69;    // a4
            const float vel  = args.size() > 1 ? (float) (double) args[1] : 1.0f;
            const int   ms   = args.size() > 2 ? (int) args[2] : 800;

            proc_.triggerTestNote (note, vel, ms);

            auto* o = new juce::DynamicObject();
            o->setProperty ("note", note);
            o->setProperty ("blocks", (juce::int64) proc_.blocksSeen());
            done (juce::var (o));
        })
        // ── MIDI CC mapping (synthis-cda.1) ─────────────────────────────────
        // The mapping table, by SLOT (macro1..macro16, attack, decay, sustain,
        // release, output), plus the input-monitor ring and learn, for the
        // shell's mapping UI. All message-thread calls into the processor.
        .withNativeFunction ("getCcMappings",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            juce::Array<juce::var> out;
            for (int t = 0; t < KromosynthAudioProcessor::kNumCcTargets; ++t)
            {
                const auto m = proc_.ccMapping (t);
                auto* o = new juce::DynamicObject();
                o->setProperty ("target", t);
                o->setProperty ("name", KromosynthAudioProcessor::ccTargetName (t));
                o->setProperty ("mapped", m.mapped);
                o->setProperty ("source", m.source);
                o->setProperty ("cc", m.ccNumber);
                o->setProperty ("channel", m.channel);
                o->setProperty ("rangeMin", (double) m.rangeMin);
                o->setProperty ("rangeMax", (double) m.rangeMax);
                o->setProperty ("invert", m.invert);
                o->setProperty ("smoothing", (double) m.smoothing);
                o->setProperty ("value", (double) proc_.ccTargetValue (t));
                out.add (juce::var (o));
            }
            done (juce::var (out));
        })
        .withNativeFunction ("getCcMonitor",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            juce::Array<juce::var> out;
            for (const auto& e : proc_.ccMonitor())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("source", e.source);
                o->setProperty ("number", e.number);
                o->setProperty ("value", e.value);
                o->setProperty ("time", (juce::int64) e.time);
                out.add (juce::var (o));
            }
            done (juce::var (out));
        })
        .withNativeFunction ("setCcLearnTarget",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // args[0]: slot 1..16 or a name (attack|decay|sustain|release|output);
            // absent or negative clears learn.
            const int t = args.size() > 0
                        ? KromosynthAudioProcessor::ccTargetFromName (args[0].toString())
                        : -1;
            proc_.setCcLearnTarget (t);
            auto* o = new juce::DynamicObject();
            o->setProperty ("learnTarget", proc_.ccLearnTarget());
            o->setProperty ("name", KromosynthAudioProcessor::ccTargetName (proc_.ccLearnTarget()));
            done (juce::var (o));
        })
        .withNativeFunction ("clearCcMapping",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const int t = args.size() > 0
                        ? KromosynthAudioProcessor::ccTargetFromName (args[0].toString()) : -1;
            if (t >= 0) proc_.clearCcMapping (t);
            done (juce::var (t >= 0));
        })
        .withNativeFunction ("setCcMapping",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // args: target, cc, channel, rangeMin, rangeMax, invert, smoothing.
            // cc < 0 clears the mapping.
            const int t = args.size() > 0
                        ? KromosynthAudioProcessor::ccTargetFromName (args[0].toString()) : -1;
            if (t < 0) { done (juce::var()); return; }

            const int   cc   = args.size() > 1 ? (int) args[1] : -1;
            const int   ch   = args.size() > 2 ? (int) args[2] : 1;
            const float mn   = args.size() > 3 ? (float) (double) args[3] : 0.0f;
            const float mx   = args.size() > 4 ? (float) (double) args[4] : 1.0f;
            const bool  inv  = args.size() > 5 && (bool) args[5];
            const float sm   = args.size() > 6 ? (float) (double) args[6] : 0.0f;
            proc_.setCcMapping (t, cc, ch, mn, mx, inv, sm);

            const auto m = proc_.ccMapping (t);
            auto* o = new juce::DynamicObject();
            o->setProperty ("target", t);
            o->setProperty ("name", KromosynthAudioProcessor::ccTargetName (t));
            o->setProperty ("mapped", m.mapped);
            o->setProperty ("source", m.source);
            o->setProperty ("cc", m.ccNumber);
            o->setProperty ("channel", m.channel);
            o->setProperty ("rangeMin", (double) m.rangeMin);
            o->setProperty ("rangeMax", (double) m.rangeMax);
            o->setProperty ("invert", m.invert);
            o->setProperty ("smoothing", (double) m.smoothing);
            done (juce::var (o));
        })
        // ── OSC receiver (synthis-cda.2) ─────────────────────────────────────
        // Port get/set, receiver state (a bind failure is reported here, not
        // swallowed) and this device's IP addresses, so the shell can show
        // where to send. All message-thread calls into the processor; the
        // receiver itself exists only in the Standalone wrapper.
        .withNativeFunction ("getOscPort",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            done (juce::var (proc_.oscPort()));
        })
        .withNativeFunction ("setOscPort",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const int port = args.size() > 0 ? (int) args[0] : KromosynthAudioProcessor::kOscDefaultPort;
            const bool ok = proc_.setOscPort (port);
            const auto st = proc_.oscState();
            auto* o = new juce::DynamicObject();
            o->setProperty ("ok", ok);
            o->setProperty ("port", st.port);
            o->setProperty ("listening", st.listening);
            o->setProperty ("error", st.error);
            done (juce::var (o));
        })
        .withNativeFunction ("getOscState",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const auto st = proc_.oscState();
            auto* o = new juce::DynamicObject();
            o->setProperty ("receiverExists", st.receiverExists);
            o->setProperty ("listening", st.listening);
            o->setProperty ("port", st.port);
            o->setProperty ("error", st.error);
            done (juce::var (o));
        })
        .withNativeFunction ("getLocalIpAddresses",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            juce::Array<juce::var> out;
            for (const auto& a : KromosynthAudioProcessor::localIpAddresses())
                out.add (a);
            done (juce::var (out));
        })
        .withNativeFunction ("removeInstrument",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const auto id = args.size() > 0 ? args[0].toString() : juce::String();
            done (juce::var (proc_.removeInstrument (id)));
        })
        .withNativeFunction ("getSpectrogram",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
           #if KROMOSYNTH_HAS_FAUST
            const auto& bytes = proc_.faustSpectrogram();

            if (bytes.empty()) { done (juce::var()); return; }

            auto* o = new juce::DynamicObject();
            o->setProperty ("bins", FaustEngine::kSpectrogramBins);
            o->setProperty ("frames", FaustEngine::kSpectrogramFrames);

            // BASE64, not an array of numbers. 96x64 is 6144 values, which as
            // JSON is around 25 kB of text to serialise, ship and parse every
            // time an instrument loads; base64 of the same bytes is 8 kB and
            // decodes in one atob call. The bridge is an event pair carrying
            // JSON, so what goes over it is worth being careful about.
            o->setProperty ("data", juce::Base64::toBase64 (bytes.data(), bytes.size()));

            done (juce::var (o));
           #else
            done (juce::var());
           #endif
        })
        .withNativeFunction ("listInstruments",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            juce::Array<juce::var> out;

            for (const auto& e : proc_.library().scan())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("id", e.id);
                o->setProperty ("name", e.name);
                // So the shell can say what will play it. A live Faust
                // instrument and a sample set behave differently enough --
                // polyphony, tail, CPU -- that hiding the difference would be
                // a disservice.
                o->setProperty ("kind", e.kind == InstrumentLibrary::Kind::faust ? "faust"
                                      : e.kind == InstrumentLibrary::Kind::wclap ? "wclap"
                                                                                 : "sampler");
                // Region count would mean loading every SFZ on every list, so
                // it is reported as 0 here and filled in on load. Stated
                // rather than silently omitted: the UI shows nothing instead
                // of a wrong number.
                o->setProperty ("regions", 0);

                // ── sidecar metadata, for sorting and filtering ────────────
                //
                // Shaped exactly like synthis.json (ARTEFACT_CONTRACT §3) so
                // the shell has one vocabulary rather than a second flattened
                // one to learn. `measured` nests for a further reason: it holds
                // trimDb and rtf, and engineState() reports LIVE values under
                // those same names. Flattened, `entry.trimDb` and
                // `state.trimDb` would look interchangeable and are not -- one
                // is what the artefact declares, the other what this machine
                // measured on load.
                //
                // ABSENT FIELDS ARE OMITTED, never sent as "" or 0. The shell
                // needs to sort unset entries last rather than treating a
                // missing discovery date as the epoch, and it can only do that
                // if undefined reaches it as undefined.
                //
                // Ptr rather than a raw pointer with a manual delete on the
                // empty branch: a block that turns out to hold nothing is the
                // common case here, and "free it yourself unless you wrapped
                // it" is a rule that only has to be forgotten once.
                const auto& m = e.meta;

                auto setIfSet = [] (const juce::DynamicObject::Ptr& target, const char* key,
                                    const juce::String& value)
                {
                    if (value.isNotEmpty())
                        target->setProperty (key, value);
                };

                {
                    juce::DynamicObject::Ptr source (new juce::DynamicObject());

                    setIfSet (source, "soundId", m.soundId);
                    setIfSet (source, "substrate", m.substrate);
                    setIfSet (source, "evolutionRunId", m.evolutionRunId);
                    setIfSet (source, "class", m.soundClass);
                    setIfSet (source, "creator", m.creator);
                    setIfSet (source, "discoveredAt", m.discoveredAt);
                    setIfSet (source, "adoptedAt", m.adoptedAt);
                    setIfSet (source, "renderedAt", m.renderedAt);
                    setIfSet (source, "acquiredAt", m.acquiredAt);

                    if (m.generation >= 0)
                        source->setProperty ("generation", m.generation);

                    if (m.hasRender)
                    {
                        juce::DynamicObject::Ptr render (new juce::DynamicObject());

                        if (m.renderDuration > 0.0) render->setProperty ("duration", m.renderDuration);
                        if (m.velocityLayers > 0)   render->setProperty ("velocityLayers", m.velocityLayers);

                        if (m.pitchLow.isNotEmpty() && m.pitchHigh.isNotEmpty())
                        {
                            juce::DynamicObject::Ptr range (new juce::DynamicObject());
                            range->setProperty ("low", m.pitchLow);
                            range->setProperty ("high", m.pitchHigh);
                            render->setProperty ("pitchRange", juce::var (range.get()));
                        }

                        source->setProperty ("render", juce::var (render.get()));
                    }

                    if (source->getProperties().size() > 0)
                        o->setProperty ("source", juce::var (source.get()));
                }

                {
                    juce::DynamicObject::Ptr character (new juce::DynamicObject());
                    juce::DynamicObject::Ptr labels (new juce::DynamicObject());

                    setIfSet (character, "soundType", m.soundType);

                    for (int i = 0; i < InstrumentLibrary::kNumDescriptors; ++i)
                        setIfSet (labels, InstrumentLibrary::descriptorName (i), m.labels[i]);

                    if (labels->getProperties().size() > 0)
                    {
                        character->setProperty ("labels", juce::var (labels.get()));
                        // Sent with the labels, never separately: it is what
                        // says whether "bright" is a claim about this sound or
                        // about the corpus it was labelled against (§3.3).
                        character->setProperty ("labelsAreCorpusRelative",
                                                m.labelsAreCorpusRelative);
                    }

                    // The raw vector is what the shell should ORDER on. Ranking
                    // it against the rest of this list is a question about the
                    // user's own library, needs no corpus, and cannot go stale
                    // when the archive relabels.
                    if (m.hasDescriptors)
                    {
                        juce::Array<juce::var> values;

                        for (int i = 0; i < InstrumentLibrary::kNumDescriptors; ++i)
                            values.add (m.descriptors[i]);

                        character->setProperty ("descriptors", juce::var (values));
                    }

                    if (character->getProperties().size() > 0)
                        o->setProperty ("character", juce::var (character.get()));
                }

                if (m.hasMeasured)
                {
                    juce::DynamicObject::Ptr measured (new juce::DynamicObject());

                    measured->setProperty ("trimDb", m.trimDb);
                    measured->setProperty ("rtf", m.rtf);
                    measured->setProperty ("capAt4x", m.capAt4x);
                    measured->setProperty ("hasGate", m.hasGate);
                    setIfSet (measured, "rtfPlatform", m.rtfPlatform);
                    // A drone is a fine pad and a poor MIDI instrument, and 48
                    // of 120 archive genomes are drones. Worth showing before
                    // the user finds out by playing one.
                    setIfSet (measured, "articulation", m.articulation);

                    o->setProperty ("measured", juce::var (measured.get()));
                }

                out.add (juce::var (o));
            }

            done (juce::var (out));
        })
        .withNativeFunction ("getLibraryRoot",
                             [] (const juce::Array<juce::var>&,
                                 juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            done (juce::var (InstrumentLibrary::root().getFullPathName()));
        })
        .withNativeFunction ("loadInstrument",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            auto* o = new juce::DynamicObject();

            if (args.isEmpty() || ! args[0].isString())
            {
                o->setProperty ("ok", false);
                o->setProperty ("detail", "loadInstrument expects an id string");
                done (juce::var (o));
                return;
            }

            const auto r = proc_.loadInstrument (args[0].toString());

            o->setProperty ("ok", r.ok);
            o->setProperty ("id", r.id);
            o->setProperty ("regions", r.numRegions);
            o->setProperty ("tailSeconds", r.tailSeconds);
            o->setProperty ("detail", r.detail);

            // The Faust path returns as soon as the module is loaded and hands
            // the measurement to a background thread, so "ok" here means the
            // load started, not that the instrument is ready. The shell polls
            // isMeasuring until this clears.
            o->setProperty ("measuring", proc_.isMeasuring());

            done (juce::var (o));
        })
        .withNativeFunction ("logFromShell",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // The page has no other way to leave a trace. Everything the shell
            // does is invisible in the log, so "the UI said it failed" and
            // "the processor said it succeeded" could only be reconciled by
            // guessing -- twice, wrongly, before this existed.
            proc_.logEvent ("shell: " + (args.isEmpty() ? juce::String() : args[0].toString()));
            done (juce::var (true));
        })
        .withNativeFunction ("getLoadState",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // POLLED, not pushed, for the reason AcquisitionRunner records: a
            // push has to cross threads into an editor that may have been
            // destroyed since the work began, and a poll cannot.
            auto* o = new juce::DynamicObject();
            const auto r = proc_.lastLoadOutcome();

            o->setProperty ("measuring", proc_.isMeasuring());
            o->setProperty ("ok", r.ok);
            o->setProperty ("id", r.id);
            o->setProperty ("regions", r.numRegions);
            o->setProperty ("tailSeconds", r.tailSeconds);
            o->setProperty ("detail", r.detail);

            done (juce::var (o));
        })
        .withNativeFunction ("getStatus",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const auto r = proc_.currentInstrument();

            auto* o = new juce::DynamicObject();
            o->setProperty ("ok", r.ok);
            o->setProperty ("id", r.id);
            o->setProperty ("regions", r.numRegions);
            o->setProperty ("tailSeconds", r.tailSeconds);
            done (juce::var (o));
        })
        .withNativeFunction ("openSite",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            showSite (true);
            done (juce::var (true));
        })
        .withNativeFunction ("chromeHeight",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // THE PAGE MEASURES ITS OWN BAR AND WE BELIEVE IT.
            //
            // kStripHeight is only the bar; the element is that plus
            // env(safe-area-inset-top), and nothing on this side can compute
            // the second term for the rectangle we actually occupy. The first
            // version of this assumed 50pt and the bar rendered 62pt below the
            // strip's own bottom edge -- measured, AUV3_IOS_FINDINGS C35.
            //
            // Clamped rather than trusted blindly. A page is not a threat here
            // -- it is ours, served from this binary, and cannot navigate away
            // -- but a layout bug reporting 0 would hide the chrome entirely
            // and one reporting 5000 would hide the content, and neither should
            // be reachable from a CSS mistake.
            const auto reported = args.size() > 0 ? (int) args[0] : 0;
            const auto clamped  = juce::jlimit (kromosynth::shell::kStripHeight,
                                                kromosynth::shell::kStripHeight * 6,
                                                reported);

            // LOGGED UNCONDITIONALLY, and the first version was not.
            //
            // It logged only when the value CHANGED, which made "the page never
            // called" and "the page reported the seed value" produce identical
            // silence -- and that is exactly the pair I then had to tell apart.
            // C25's rule about writing the check that refuses rather than the
            // one you read applies to instruments too.
            proc_.logEvent ("chrome_height reported=" + juce::String (reported)
                            + " using=" + juce::String (clamped)
                            + " was=" + juce::String (stripHeight_)
                            + " args=" + juce::String (args.size()));

            if (clamped != stripHeight_)
            {
                stripHeight_ = clamped;

                // Only matters while the site is up -- that is the one state
                // where this number decides anything. resized() is cheap and
                // unconditional is one fewer branch to get wrong.
                resized();
            }

            done (juce::var (clamped));
        })
        .withNativeFunction ("getAuthState",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // WHAT THE SITE LAST SAID, labelled as such.
            //
            // `kind` is "unknown" until the first sentinel arrives, and the
            // page renders nothing for it. That is deliberate: the bar showing
            // "signed out" before it has been told anything would be asserting
            // a state it has not observed, which is the failure C40 lists as
            // this codebase's most repeated.
            //
            // Anonymous is its OWN kind, not folded into either neighbour.
            // Signing out on the web app silently creates an anonymous
            // identity and likes attach to it; it looked identical to being
            // signed in until this existed, which is the defect this whole
            // path was written for.
            auto* o = new juce::DynamicObject();
            o->setProperty ("kind",   kromosynth::auth::toWire (authState_.kind));
            o->setProperty ("label",  authState_.label);
            o->setProperty ("unread", authState_.unread);

            done (juce::var (o));
        })
        .withNativeFunction ("getActivity",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // The PULL half of the activity push (synthis-7o3.2): the page
            // calls this on "activityChanged" and on its own timer while
            // anything is active, so a throttled or lost push costs one stale
            // frame, never wrong state. The event itself carries no data.
            done (activity_.toJson());
        })
        .withNativeFunction ("showLibrary",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // The way back, which the page could not ask for until the nav was
            // in it -- the native bar held it as a `showShell` callback; the
            // strip's Library tab needs it as a call.
            //
            // The site is NOT navigated away from and NOT unloaded -- it keeps
            // `withKeepPageLoadedWhenBrowserIsHidden`, so coming back resumes
            // wherever the user was, half-completed sign-in included.
            showSite (false);

            // Opening the Library is what the landed badge counts as "seen".
            if (activity_.clearUnseen())
                pushActivity();

            done (juce::var (true));
        })
        .withNativeFunction ("openSitePath",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // Unlike openSite, this NAVIGATES -- showSite loads lazily and only
            // once, deliberately, so that reopening the site does not throw
            // away wherever the user had got to. Asking for a specific place is
            // the one case where replacing that is the request.
            const auto path = args.size() > 0 ? args[0].toString() : juce::String();

            if (path.isEmpty() || ! path.startsWith ("/"))
            {
                // Only site-relative paths. Handing this an absolute URL would
                // make a native function that navigates the embedded browser
                // anywhere the page asks, which is a bigger capability than the
                // shell needs.
                done (juce::var (false));
                return;
            }

            showSite (true);
            navigateSiteTo (path);
            done (juce::var (true));
        })
        .withNativeFunction ("soundsView",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // WHICH SUB-TAB OWNS THE BODY. Only the page knows, so only the
            // page can say. On iOS there is no RenderList -- drag does not
            // exist there and the class compiles out -- so this is a no-op and
            // the page's own HTML list stays visible in the body. Desktop
            // shrinks the shell to its strip and hands the rest to the native
            // list, which is the only way those rows can be draggable.
            const bool on = args.size() > 0 && (bool) args[0];

            if (on != soundsVisible_)
            {
                soundsVisible_ = on;
                resized();
                proc_.logEvent (juce::String ("sounds_view=") + (on ? "1" : "0"));
            }

            done (juce::var (true));
        })
        .withNativeFunction ("renderPlaying",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // The page owns the audio, so it reports what is actually sounding
            // rather than the list assuming its own button press worked. A
            // file that failed to load leaves the row unlit, which is correct.
           #if ! JUCE_IOS
            if (renderList_ != nullptr)
                renderList_->setPlayingFile (args.size() > 0 ? args[0].toString() : juce::String());
           #endif

            done (juce::var (true));
        })
        .withNativeFunction ("revealRenders",
                             [] (const juce::Array<juce::var>&,
                                 juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // The escape hatch that always works, whatever the list and the drag
            // do. createDirectory first because the folder does not exist until
            // the first take, and revealing nothing looks like a broken button
            // rather than an empty shelf.
            auto dir = kromosynth::renders::outbox();

            dir.createDirectory();
            dir.revealToUser();

            done (juce::var (true));
        })
        .withNativeFunction ("getRenders",
                             [] (const juce::Array<juce::var>&,
                                 juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // WHAT IS ACTUALLY ON DISK, which is the point of this function
            // existing at all. Both "taken" markers in the web app are guesses:
            // the live one assumes any live artefact on screen is installed, and
            // the single one remembers only this session. Neither can see the
            // filesystem. This can, so the Renders view built on it is the first
            // thing in the plugin that reports delivery by observing it.
            //
            // Duration comes from each file's own header rather than from the
            // render request, so it cannot repeat C44's mistake of showing the
            // length that was asked for instead of the one that arrived.
            juce::Array<juce::var> out;

            for (const auto& r : kromosynth::renders::list())
            {
                auto* o = new juce::DynamicObject();

                // The FILE NAME, not the display name: this is the handle the
                // page passes back for bytes or a delete, and resolve() accepts
                // nothing else.
                o->setProperty ("file", r.file.getFileName());
                o->setProperty ("name", r.name);
                o->setProperty ("seconds", r.seconds);
                o->setProperty ("bytes", (double) r.bytes);
                o->setProperty ("modifiedMs", (double) r.modifiedMs);

                out.add (juce::var (o));
            }

            done (juce::var (out));
        })
        .withNativeFunction ("deleteRender",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // Deletes a file the user spent extraction quota on, so the page is
            // expected to confirm first. This end does not prompt -- a native
            // modal over a webview inside a DAW is its own problem -- but it
            // does refuse anything resolve() will not vouch for, and it reports
            // what it did rather than assuming success.
            const auto name = args.size() > 0 ? args[0].toString() : juce::String();
            const auto target = kromosynth::renders::resolve (name);

            if (! target.existsAsFile())
            {
                proc_.logEvent ("render_delete_refused " + name.quoted());
                done (juce::var (false));
                return;
            }

            // Ableton leaves an .asd analysis sidecar beside any wav it has
            // read. Deleting the wav and leaving that behind would accumulate
            // orphans in a folder the user is being encouraged to browse.
            const auto sidecar = target.getSiblingFile (target.getFileName() + ".asd");

            if (sidecar.existsAsFile())
                sidecar.deleteFile();

            const bool gone = target.deleteFile();

            proc_.logEvent ("render_deleted ok=" + juce::String (gone ? 1 : 0)
                            + " " + target.getFileName().quoted());

            done (juce::var (gone));
        })
        .withNativeFunction ("getAcquisitions",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // Polled by the shell rather than pushed from the worker thread.
            // A push would have to cross threads into an editor that may have
            // been destroyed since the download started; a poll cannot.
            juce::Array<juce::var> out;

            for (const auto& s : proc_.acquisitions().snapshot())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("id", s.artifactId);
                o->setProperty ("message", s.message);
                o->setProperty ("progress", s.progress);
                o->setProperty ("finished", s.finished);
                o->setProperty ("ok", s.ok);
                // So the shell can word its summary line for what actually
                // happened. Two jobs, two different outcomes, one poll.
                o->setProperty ("kind",
                                s.verb == kromosynth::acquisition::Verb::render ? "render"
                                                                               : "instrument");
                out.add (juce::var (o));
            }

            done (juce::var (out));
        })
        .withNativeFunction ("clearFinishedAcquisitions",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            proc_.acquisitions().clearFinished();
            done (juce::var (true));
        });

    // ── Computer-keyboard play, STANDALONE ONLY (synthis-o2a.5) ──────────
    //
    // On macOS a focused WKWebView takes the key presses, so JUCE key handling
    // on the MidiKeyboardComponent below only works while that component itself
    // has focus. The shell page therefore forwards keydown / keyup / blur here
    // (see ShellPage.h), and the notes go onto the same proc_.keyboardState the
    // on-screen keyboard uses, so processBlock merges them exactly like a host
    // note.
    //
    // Registered ONLY under the Standalone wrapper: in AU, VST3 and AUv3 a DAW
    // host owns the MIDI input, the page's __juce__functions list will not
    // contain "keyEvent", and the page never calls it.
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        options = options.withNativeFunction ("keyEvent",
            [this] (const juce::Array<juce::var>& args,
                    juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // args: [down, key, repeat] for a key event, or ["blur"] when the
            // window lost focus and every held key must be released.
            if (args.size() > 0 && args[0].isString() && args[0].toString() == "blur")
            {
                releaseHeldKeys();
            }
            else if (args.size() >= 2)
            {
                if (keyLogBudget_ > 0)
                {
                    --keyLogBudget_;
                    proc_.logEvent ("key_event route=page down=" + juce::String ((bool) args[0] ? 1 : 0)
                                    + " key=" + args[1].toString()
                                    + " repeat=" + juce::String (args.size() > 2 && (bool) args[2] ? 1 : 0));
                }

                handleKeyEvent ((bool) args[0], args[1].toString(),
                                args.size() > 2 && (bool) args[2]);
            }

            done (juce::var (true));
        });

    browser_ = std::make_unique<InstrumentedBrowser> (options, proc_);
    browser_->allowedOrigin = juce::WebBrowserComponent::getResourceProviderRoot();
    // ── keyboard focus ──────────────────────────────────────────────────
    //
    // SPECULATIVE, and marked as such because it is not verified from this
    // side: Björn's GarageBand Musical Typing keys latch in the DOWN position
    // after a press with this plugin and not with others, which is GarageBand's
    // OWN UI state. A plugin cannot draw those keys, so the host never saw the
    // key-up -- which is also exactly why no note-off arrived.
    //
    // Our editor is a WKWebView; GarageBand's own instruments have native
    // views. A WKWebView becomes AppKit first responder when clicked, and the
    // shell has no text input to justify holding focus. Refusing it costs
    // nothing here and is the one thing on this side that could plausibly be
    // the cause.
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    browser_->setWantsKeyboardFocus (false);
    browser_->setMouseClickGrabsKeyboardFocus (false);

    addAndMakeVisible (*browser_);

    // ── The site ─────────────────────────────────────────────────────────────
    //
    // Native integration OFF and allowedOrigin EMPTY, both deliberate and both
    // the opposite of the shell above. Off-origin navigation has to be allowed
    // here: signing in redirects to an identity provider, and blocking that
    // would break the only thing this surface exists for. That is safe only
    // because there is no bridge on this component to carry off-origin -- which
    // is precisely the pairing WP section 12 requires, and why this cannot be
    // one component with a flag.
    //
    // Created but not shown. The shell is what opens on launch, because the
    // shell is the surface that works offline.
    site_ = std::make_unique<InstrumentedBrowser> (
        Options{}.withKeepPageLoadedWhenBrowserIsHidden(), proc_);
    site_->interceptsSentinels = true;

    // Stored, not acted on. The editor holds the last thing the site said and
    // hands it to the page when asked; nothing here changes what the plugin
    // does, only what the bar can show.
    site_->onAuthState = [this] (const kromosynth::auth::State& s) { authState_ = s; };

    // The progress sentinel feeds the activity list (synthis-7o3.2); the page
    // is told about every change with a content-free activityChanged push.
    site_->onProgress = [this] (const kromosynth::progress::Event& ev) { onProgressEvent (ev); };

    // A full load of the site view kills every follower on the old document.
    site_->onSiteReloaded = [this]
    {
        if (activity_.siteReloaded())
            pushActivity();
    };

    // The landed push: AcquisitionRunner finishes on the message thread, and
    // the editor is the one that owns the activity list. Cleared in the
    // destructor; the SafePointer guards the case where this editor is gone by
    // the time the message arrives (a download outlives the editor).
    proc_.acquisitions().setLandedHandler (
        [safeEditor = juce::Component::SafePointer<KromosynthAudioProcessorEditor> (this)]
        (juce::String artifactId, bool ok, juce::String name,
         kromosynth::acquisition::Verb verb)
        {
            if (safeEditor == nullptr)
                return;

            safeEditor->onAcquisitionFinished (artifactId, ok, name, verb);
        });

    addChildComponent (*site_);

    // Sentinel rate probe only (synthis-7o3.1; see pageFinishedLoading): the
    // site loads lazily, so a measurement run has to open it itself or the
    // probe never has a page to run on. Inert unless the variable is set.
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone
        && juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_SENTINEL_PROBE", {}).isNotEmpty())
    {
        juce::Component::SafePointer<KromosynthAudioProcessorEditor> safeEditor (this);
        juce::Timer::callAfterDelay (2500, [safeEditor]
        {
            if (safeEditor != nullptr)
                safeEditor->showSite (true);
        });

        // KROMOSYNTH_SENTINEL_PROBE_HIDE=1 puts the site back behind the shell a
        // few seconds after the probe script is injected. That is the state the
        // user is in while watching the Library tab, and a hidden WKWebView
        // throttles its timers, which decides how patient a "lost track of this
        // job" timeout has to be.
        if (juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_SENTINEL_PROBE_HIDE", {}) == "1")
        {
            juce::Timer::callAfterDelay (14000, [safeEditor]
            {
                if (safeEditor != nullptr)
                    safeEditor->showSite (false);
            });
        }
    }
#endif

    // ── The on-screen keyboard, STANDALONE ONLY ─────────────────────────
    //
    // wrapperType, not a compile-time switch: the same sources build the AU,
    // VST3, AUv3 and Standalone, and the keyboard must exist in NONE of the
    // first three — a DAW host owns the MIDI input there. The state it reads
    // lives in the processor (proc_.keyboardState), which processBlock merges
    // into the host's MidiBuffer, so the notes reach whichever engine is active
    // by the same route a host note takes.
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        keyboard_ = std::make_unique<juce::MidiKeyboardComponent> (
            proc_.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
        keyboard_->setLowestVisibleKey (36);   // C2; about two octaves visible at the default width
        keyboard_->setScrollButtonsVisible (true);
        // The component's own key mapping starts at JUCE's default base octave
        // 6, which plays nothing the visible keyboard shows. Pin it to the
        // octave the view starts on (3 = C2 = note 36), so a key played with the
        // component focused and the same key played from the QWERTY keyboard
        // (synthis-o2a.5) sound the same note. Z/X move both together.
        keyboard_->setKeyPressBaseOctave (keyOctave_);
        addAndMakeVisible (*keyboard_);

        // The octave readout, at the right end of the strip beside the
        // keyboard's up-octave scroll button, so the current octave is visible
        // while playing from the computer keyboard.
        octaveLabel_ = std::make_unique<juce::Label> ("keyOctave", juce::String());
        octaveLabel_->setColour (juce::Label::backgroundColourId,
                                 kromosynth::palette::colour (kromosynth::palette::surfaceBase));
        octaveLabel_->setColour (juce::Label::textColourId,
                                 kromosynth::palette::colour (kromosynth::palette::textBody));
        octaveLabel_->setColour (juce::Label::outlineColourId,
                                 kromosynth::palette::colour (kromosynth::palette::border));
        octaveLabel_->setJustificationType (juce::Justification::centred);
        octaveLabel_->setFont (juce::Font (juce::FontOptions (13.0f)));
        updateOctaveLabel();
        addAndMakeVisible (*octaveLabel_);
    }

    // Resizable with a wide range, because "does it resize" is a gate
    // question and a fixed-size editor cannot answer it. Lower bound is
    // small enough to catch layout collapse; upper bound is large enough
    // that a 4K panel is not artificially clamped.
#if JUCE_IOS
    // A PHONE IS NARROWER THAN THE DESKTOP MINIMUM, and the minimum wins.
    //
    // The standalone wrapper sizes the editor to the window, but setResizeLimits
    // clamps it afterwards. An iPhone 13 Pro Max is 428 x 926 points in
    // portrait, so a 640 lower bound left 212 points off the right of the
    // screen -- which is exactly how the shell's own buttons and the right edge
    // of the site both vanished in portrait and reappeared in landscape, where
    // the 926-point width has room for 640. The log said so all along:
    // "editor_resized w=640 h=926" on a 428-point display.
    //
    // Sized to the display's userArea rather than to a guess, because that is
    // the safe area after the status bar and home indicator, and it is right on
    // every device rather than on the one being tested.
    setResizable (true, true);

    const auto safeArea = juce::Desktop::getInstance().getDisplays()
                              .getPrimaryDisplay()->userArea;

    // 320 is the narrowest iPhone in points. The lower bound still exists so a
    // layout collapse would be visible rather than absorbed.
    setResizeLimits (320, 400, 3840, 2400);
    setSize (safeArea.getWidth(), safeArea.getHeight());
#else
    setResizable (true, true);
    // A plain 400: the Sounds list takes the body instead of a slice of it, so
    // there is nothing to compensate for. The standalone keyboard's strip
    // (kKeyboardHeight) is reserved inside resized() on top of this minimum,
    // not added to it.
    setResizeLimits (640, 400, 3840, 2400);
    setSize (1280, 800);
#endif

#if ! JUCE_IOS
    // Constructed before the browsers, so the first resized() already has it
    // and no frame is laid out with the gutter's rectangle handed to a webview
    // that would then have to give it back.
    renderList_ = std::make_unique<RenderList>();
    renderList_->onLog = [this] (const juce::String& line) { proc_.logEvent (line); };

    // Playback belongs to the page: WebAudio reaches the system output, whereas
    // routing a preview through the processor would drop it into the host's
    // signal chain and get it recorded if the transport is rolling.
    renderList_->onPlayRequest = [this] (const juce::String& file)
    {
        if (browser_ != nullptr)
            browser_->emitEventIfBrowserIsVisible ("playRender", juce::var (file));
    };

    // The Sounds badge lives in the page's sub-tab row, so the page has to hear
    // about a delete here or a take landing while the list is open.
    renderList_->onContentsChanged = [this]
    {
        if (browser_ != nullptr)
            browser_->emitEventIfBrowserIsVisible ("rendersChanged", juce::var (true));
    };

    // Dismissing the band, or a take arriving while it is hidden, changes how
    // much room the site gets -- so the layout has to be redone, which only
    // the editor can do.
    renderList_->onHandleWantedChanged = [this] { resized(); };

    // NOT addAndMakeVisible: invisible until the Sounds sub-tab asks for it, so
    // it costs nothing on every other tab.
    addChildComponent (*renderList_);
#endif


#if ! KROMOSYNTH_EDITOR_STUB
    // The shell, not the site. The site is a second component with native
    // integration OFF; see WP section 12. Not built yet -- acquisition needs
    // the sentinel-URL hook on the web app side.
    // A unique URL per editor, as cheap insurance against the page being
    // cached from the custom scheme.
    //
    // HONEST PROVENANCE: this was added on a WRONG diagnosis. Three shell
    // fixes appeared to have no effect and a diagnostic added to trace them
    // logged nothing, and I concluded WKWebView was serving a cached page. It
    // was not. AUHostingServiceXPC_arrow had been running since 11:57 and
    // loads the plugin binary ONCE -- every build installed afterwards was
    // never loaded at all, because reopening the plugin WINDOW makes a new
    // processor inside the same image. `processor_constructed` appearing
    // repeatedly is not evidence that new code is running.
    //
    // Page caching was never demonstrated. This stays because it costs one
    // string and removes a whole class of doubt from future "my fix had no
    // effect" hunts -- but it is not the fix for anything observed here, and
    // the thing to check first is whether the host process predates the build.
    const auto shellRoot = juce::WebBrowserComponent::getResourceProviderRoot()
                         + "?v=" + juce::String (juce::Time::getHighResolutionTicks());

    proc_.logEvent ("navigating to shell " + shellRoot);
    browser_->goToURL (shellRoot);
#endif

    // The activity lease runs on this timer (timerCallback), so it runs in
    // EVERY build, not only the WCLAP one.
    startTimer (250);
}

KromosynthAudioProcessorEditor::~KromosynthAudioProcessorEditor()
{
    stopTimer();

    // The landed handler is editor-set; clear it so a download finishing after
    // this editor is gone does not call into it.
    proc_.acquisitions().setLandedHandler (nullptr);

    // A key still held on the on-screen keyboard when the editor goes away
    // must not stay stuck — a lost touch is exactly the case where no note-off
    // will ever arrive. The state lives in the processor, and the next block's
    // processNextMidiBuffer injects the note-offs. No-op in AU/VST3/AUv3,
    // where no key was ever pressed.
    proc_.keyboardState.allNotesOff (0);

   #if KROMOSYNTH_HAS_WCLAP
    engineView_.reset();   // closes the engine's gui before the processor can unload the engine
   #endif
    proc_.logEvent ("editor_destroyed");
}

void KromosynthAudioProcessorEditor::focusLost (juce::Component::FocusChangeType cause)
{
    // Losing focus must release every key still held from the computer
    // keyboard (synthis-o2a.5): no key-up will ever arrive for them. The shell
    // page reports the same event from its own window blur; this covers the
    // cases where the webview does not fire one. No-op unless a key was held,
    // which only the Standalone-only keyEvent path can set.
    releaseHeldKeys();

    juce::AudioProcessorEditor::focusLost (cause);
}

// ── Computer-keyboard play (synthis-o2a.5), STANDALONE ONLY ────────────────
//
// Reached only through the "keyEvent" native function, which is registered
// under the Standalone guard in the constructor; the wrapper check is repeated
// here so no future caller can feed keys in AU / VST3 / AUv3, where a DAW host
// owns the MIDI input.

void KromosynthAudioProcessorEditor::handleKeyEvent (bool down, const juce::String& key, bool repeat)
{
    if (proc_.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
        return;

    const auto k = key.toLowerCase();
    if (k.isEmpty())
        return;

    // Z lowers, X raises the octave by one.
    if (k == "z" || k == "x")
    {
        if (down && ! repeat)
            setKeyOctave (keyOctave_ + (k == "x" ? 1 : -1));
        return;
    }

    // Optional extra: C lowers, V raises the velocity of computer-key notes.
    if (k == "c" || k == "v")
    {
        if (down && ! repeat)
            keyVelocity_ = juce::jlimit (0.1f, 1.0f, keyVelocity_ + (k == "v" ? 0.1f : -0.1f));
        return;
    }

    // The common DAW layout, identical to MidiKeyboardComponent's own default
    // mapping ("awsedftgyhujkolp;"): A S D F G H J K are the white keys and
    // W E T Y U the black ones, each an offset from the base C of the current
    // octave — the same arithmetic the component applies to its key presses.
    static const char* const kPlayKeys = "awsedftgyhujkolp;";
    const auto offset = [&k]() -> int
    {
        for (int i = 0; kPlayKeys[i] != '\0'; ++i)
            if (kPlayKeys[i] == k[0])
                return i;
        return -1;
    }();

    if (offset < 0)
        return;

    if (down)
    {
        // Auto-repeat must not retrigger: a key already held is already on.
        if (! repeat && ! heldKeyNotesByKey_.contains (k))
        {
            const int note = 12 * keyOctave_ + offset;
            if (note < 0 || note > 127)
                return;

            // Remember the sounding note per physical key, so the key-up sends
            // the note-off for THIS note even if Z/X moved the octave between
            // the key-down and the key-up.
            heldKeyNotesByKey_.set (k, note);
            proc_.keyboardState.noteOn (1, note, keyVelocity_);
        }
    }
    else
    {
        // A key release sends the note off for the note this key actually
        // started — remembered at key-down, not recomputed from the octave
        // current now, which Z/X may have moved since the key went down.
        if (heldKeyNotesByKey_.contains (k))
        {
            const int note = heldKeyNotesByKey_[k];
            heldKeyNotesByKey_.remove (k);
            proc_.keyboardState.noteOff (1, note, 0.0f);
        }
    }
}

void KromosynthAudioProcessorEditor::setKeyOctave (int octave)
{
    // Clamped to the playable range: the highest mapped key (';', offset 16)
    // must stay inside MIDI's 0..127, so 12 * octave + 16 <= 127 -> octave <= 9.
    keyOctave_ = juce::jlimit (0, 9, octave);

    if (keyboard_ != nullptr)
    {
        // Keep the on-screen keyboard on the same octave: its own key mapping
        // (so keys played with the component focused match), and the visible
        // range, moved exactly as the on-screen octave buttons move it.
        keyboard_->setKeyPressBaseOctave (keyOctave_);
        keyboard_->setLowestVisibleKey (12 * keyOctave_);
    }

    updateOctaveLabel();
}

void KromosynthAudioProcessorEditor::updateOctaveLabel()
{
    if (octaveLabel_ != nullptr)
        octaveLabel_->setText ("Octave " + juce::String (keyOctave_) + " (Z/X)",
                               juce::NotificationType::dontSendNotification);
}

bool KromosynthAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (proc_.wrapperType != juce::AudioProcessor::wrapperType_Standalone || keyboard_ == nullptr)
        return false;

    // Command and Control combinations belong to the OS and the window.
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;

    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (c != 'z' && c != 'x' && c != 'c' && c != 'v')
        return false;

    if (keyLogBudget_ > 0)
    {
        --keyLogBudget_;
        proc_.logEvent ("key_event route=native key=" + juce::String::charToString (c));
    }

    // JUCE reports auto-repeat as fresh presses. Without the guard, holding Z
    // would race the octave to its limit.
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastOctaveKeyMs_ >= 180)
    {
        lastOctaveKeyMs_ = now;
        handleKeyEvent (true, juce::String::charToString (c), false);
    }

    // Consumed either way, so the OS does not beep.
    return true;
}

void KromosynthAudioProcessorEditor::releaseHeldKeys()
{
    // The window lost focus: no key-up will ever arrive for a key still held,
    // so every one of them goes off now. The map is only cleared after the
    // loop — a non-const call invalidates its iterators.
    for (auto i = heldKeyNotesByKey_.begin(); i != heldKeyNotesByKey_.end(); ++i)
        proc_.keyboardState.noteOff (1, i.getValue(), 0.0f);

    heldKeyNotesByKey_.clear();
}

void KromosynthAudioProcessorEditor::timerCallback()
{
    // The activity lease (synthis-7o3.2): indeterminate after 60 s of
    // silence, lost after 180 s, no silence clock while the site view is
    // hidden, 25 min hard cap, terminal entries expire after 8 s.
    if (activity_.tick())
        pushActivity();

   #if KROMOSYNTH_HAS_WCLAP
    syncEngineView();
   #endif
}

void KromosynthAudioProcessorEditor::onProgressEvent (const kromosynth::progress::Event& ev)
{
    if (activity_.apply (ev))
        pushActivity();
}

void KromosynthAudioProcessorEditor::onAcquisitionFinished (const juce::String& artifactId,
                                                            bool ok,
                                                            const juce::String& name,
                                                            kromosynth::acquisition::Verb verb)
{
    // The landed push: the moment the runner finishes, not when the page's
    // poll happens to wake up. Joins on the artefact id the delivering
    // sentinel carried; an acquisition with no known job still lands, titled
    // from the request's name. A failed download marks the matching entry
    // failed — with no known job there is nothing to mark, the shell's own
    // poll already shows the failure message.
    const bool changed = ok
        ? activity_.land (artifactId, name, verb == kromosynth::acquisition::Verb::acquire)
        : activity_.fail (artifactId, "network");

    if (changed)
        pushActivity();
}

void KromosynthAudioProcessorEditor::pushActivity()
{
    // Content-free, like rendersChanged: the page answers with getActivity.
    if (browser_ != nullptr)
        browser_->emitEventIfBrowserIsVisible ("activityChanged", juce::var (true));
}

#if KROMOSYNTH_HAS_WCLAP
void KromosynthAudioProcessorEditor::syncEngineView()
{
    auto* eng = proc_.wclapEngine();
    const bool want = eng != nullptr && eng->isLoaded()
                   && juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_WCLAP_UI", "1") != "0";

    if (engineView_ != nullptr && (! want || engineView_->generation() != eng->generation()))
    {
        // The guest instance the page was talking to is gone (another instrument, or a re-activation):
        // the page's picture of the parameters is stale, so rebuild rather than patch.
        engineView_.reset();
        resized();
    }

    if (engineView_ != nullptr || ! want || engineViewFailedGeneration_ == eng->generation())
        return;

    auto view = std::make_unique<WclapEngineView> (*eng, [this] (const juce::String& line) { proc_.logEvent (line); });

    if (! view->ok())
    {
        engineViewFailedGeneration_ = eng->generation();   // do not retry every tick
        return;
    }

    engineView_ = std::move (view);
    addAndMakeVisible (*engineView_);

    // Room for the page beside a shell of at least kShellMinWidth, never wider than the screen. The page
    // says its real size a moment after it starts (Surge XT: 1130x711, where get_size said 904x569), so fit
    // again then.
    auto fit = [this]
    {
        if (engineView_ == nullptr) return;
        const auto pref = engineView_->preferredSize();
        const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
        const int w = juce::jmin (screen.getWidth(), kShellMinWidth + (pref.x > 0 ? pref.x : 900));
        const int h = juce::jmin (screen.getHeight(), juce::jmax (400, pref.y));
        setSize (w, h);
        resized();
        proc_.logEvent ("wclap_ui_editor_size editor=" + juce::String (getWidth()) + "x" + juce::String (getHeight())
                        + " engineGui=" + juce::String (pref.x) + "x" + juce::String (pref.y));
    };
    engineView_->onPageSize = [fit] (juce::Point<int>) { fit(); };
    fit();
}
#endif

void KromosynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Visible only in the moment before the page paints, and behind it if the
    // webview fails to create at all — in which case a plain dark rectangle
    // with nothing in it is itself the finding.
    g.fillAll (juce::Colour::fromRGB (0x10, 0x12, 0x14));

#if KROMOSYNTH_BROWSER_INSET > 0
    // §A3.1c occlusion test. Strip reserved along the bottom and right so
    // JUCE's ResizableCornerComponent (18x18, bottom-right) is not covered by
    // the native WKWebView NSView. Coloured so it is immediately obvious the
    // inset actually applied.
    g.setColour (juce::Colour::fromRGB (0xd9, 0x7a, 0x1e));
    g.fillRect (getLocalBounds().removeFromBottom (KROMOSYNTH_BROWSER_INSET));
    g.fillRect (getLocalBounds().removeFromRight  (KROMOSYNTH_BROWSER_INSET));
#endif
}

void KromosynthAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // THE ON-SCREEN KEYBOARD'S STRIP, taken off the bottom FIRST so nothing
    // below (the render-list handle) can overlap it — two native components
    // must not share a rectangle, and a native component over a webview is
    // invisible. Exists only in the Standalone wrapper; the webviews take the
    // rest. The shell's own chromeHeight() reporting is unaffected: the strip
    // is below the shell's rectangle, not inside it.
    if (keyboard_ != nullptr)
    {
        auto strip = bounds.removeFromBottom (kKeyboardHeight);

        // The octave readout takes the right end of the strip, immediately
        // beside the keyboard's up-octave scroll button.
        if (octaveLabel_ != nullptr)
            octaveLabel_->setBounds (strip.removeFromRight (kOctaveLabelWidth));

        keyboard_->setBounds (strip);

        // Give the on-screen keyboard keyboard focus once, after the window has
        // had time to appear: with nothing focused a key reaches only the window
        // and nobody consumes it (macOS beeps). Focused, the component plays the
        // note keys itself and Z/X bubble up to keyPressed below. Clicking the
        // page later hands focus to the web view, whose forwarding takes over.
        if (! keyboardFocusRequested_ && proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        {
            keyboardFocusRequested_ = true;
            keyboard_->setWantsKeyboardFocus (true);
            juce::Timer::callAfterDelay (400, [safe = juce::Component::SafePointer<juce::Component> (keyboard_.get())]
            {
                if (safe != nullptr)
                    safe->grabKeyboardFocus();
            });
        }

        // About two octaves visible at whatever width the keyboard has — 28
        // white keys is two octaves — floored so a key never gets narrower
        // than a touch target on a phone. setKeyWidth no-ops when the value
        // is unchanged, so this costs nothing on resizes that keep it.
        keyboard_->setKeyWidth (juce::jmax (24.0f, strip.getWidth() / 28.0f));
    }

   #if KROMOSYNTH_HAS_WCLAP
    if (engineView_ != nullptr)
    {
        const auto pref = engineView_->preferredSize();
        const int w = juce::jmin (pref.x > 0 ? pref.x : 900, bounds.getWidth() - kShellMinWidth);
        engineView_->setVisible (w >= 200);
        if (w >= 200) engineView_->setBounds (bounds.removeFromRight (w));
    }
   #endif


    if (browser_ != nullptr)
    {
        // THE SHELL'S RECTANGLE IS WHAT SAYS WHICH SURFACE IS IN CHARGE.
        //
        // The shell can never be hidden, because the chrome is inside its
        // document. So what changes is how much room it gets: the whole area
        // for the library, a strip when the site is up.
        //
        // Nothing is reserved at the top any more. The native bar used to take
        // kBarHeight off here, and before THAT it took `siteVisible_ ? 32 : 0`,
        // which made moving between surfaces a full-view mode change rather
        // than a change of content. Both are gone: the chrome is in the shell's
        // own rectangle and cannot go out of step with it.
       #if ! JUCE_IOS
        // THE ONE-ROW HANDLE, reserved off the bottom BEFORE the shell/site
        // split, so both of those shrink to accommodate it rather than being
        // overlapped by it -- a native component over a webview is invisible
        // (juce_NSViewComponent.h), which is the constraint the whole design
        // turns on.
        //
        // Only while the site is up, and only once something has been taken.
        // Browsing is exactly when you cannot reach Library > Sounds without
        // navigating away, and deleting the old gutter left that case with no
        // way to drag at all. Before the first take it costs no height.
        bool compactHandle = false;

        if (renderList_ != nullptr)
        {
            compactHandle = siteVisible_ && renderList_->wantsHandle();

            if (compactHandle)
                renderList_->setBounds (bounds.removeFromBottom (RenderList::kCompactH));
        }
       #endif

        auto shellArea = bounds;
        auto siteArea  = bounds;

        if (siteVisible_)
        {
            shellArea = bounds.withHeight (stripHeight_);
            siteArea  = bounds.withTrimmedTop (stripHeight_);
        }

       #if ! JUCE_IOS
        // THE SAME MOVE AS siteVisible_, with a native component in the
        // remainder. The shell keeps its strip -- the chrome and the sub-tabs
        // live in that document -- and the list gets everything below.
        //
        // Non-overlapping is the whole point: a native webview composites above
        // JUCE's rendering regardless of z-order (juce_NSViewComponent.h: "it'll
        // obliterate any JUCE components that may overlap this component"), so
        // the list can only own pixels the browser was never given. C45 is the
        // record of trying the alternative and finding why not.
        if (renderList_ != nullptr)
        {
            const bool sounds = soundsVisible_ && ! siteVisible_;

            renderList_->setVisible (sounds || compactHandle);

            // Compact only when it is the handle. The Sounds tab wants the
            // whole list, and setCompact is a no-op when the state matches.
            renderList_->setCompact (compactHandle && ! sounds);

            if (sounds)
            {
                shellArea = bounds.withHeight (stripHeight_);
                renderList_->setBounds (bounds.withTrimmedTop (stripHeight_));
            }
        }
       #endif

        auto area = shellArea;

        if (site_ != nullptr)
            site_->setBounds (siteArea);

#if KROMOSYNTH_BROWSER_INSET > 0
        // §A3.1c occlusion test — DIAGNOSTIC ONLY, not a kept workaround.
        //
        // Hypothesis: AU hosts do not supply their own resize affordance, so
        // JUCE's ResizableCornerComponent is the only one — and a native
        // NSView (WKWebView) composites above JUCE's software-rendered content
        // regardless of z-order, burying it. Pulling the browser back from the
        // bottom-right exposes the grip. If the AU then resizes in Live, the
        // mechanism is confirmed.
        area = area.withTrimmedBottom (KROMOSYNTH_BROWSER_INSET)
                   .withTrimmedRight  (KROMOSYNTH_BROWSER_INSET);
#endif

        browser_->setBounds (area);
    }

    if (stub_ != nullptr)
        stub_->setBounds (getLocalBounds());

    proc_.logEvent ("editor_resized w=" + juce::String (getWidth())
                    + " h=" + juce::String (getHeight()));
}

// §A3.1c discriminator content. DISPOSABLE — delete with the stub.
//
// Gated, unlike the first version. Previously this definition was compiled
// unconditionally (only the *construction* was `#if`-ed), so the literal
// "EDITOR STUB" ended up in EVERY binary — which made
//   strings <binary> | grep -c "EDITOR STUB"
// return 1 for stub and non-stub builds alike, i.e. a check that could not
// distinguish the thing it was written to distinguish.
#if KROMOSYNTH_EDITOR_STUB
void KromosynthAudioProcessorEditor::StubContent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (0x1e, 0x3a, 0x5f));

    // A border drawn at getLocalBounds() makes the answer visible without
    // reading the log: if all four edges are on screen, this component is
    // tracking the window. If the right and bottom edges are missing, it is
    // being clipped exactly as the webview was.
    g.setColour (juce::Colours::orange);
    g.drawRect (getLocalBounds(), 4);

    g.setColour (juce::Colours::white);
    g.setFont (26.0f);
    g.drawText ("EDITOR STUB - no WebBrowserComponent",
                getLocalBounds(), juce::Justification::centred, false);

    g.setFont (20.0f);
    g.drawText (juce::String (getWidth()) + " x " + juce::String (getHeight()),
                getLocalBounds().reduced (16),
                juce::Justification::centredBottom, false);
}
#else
void KromosynthAudioProcessorEditor::StubContent::paint (juce::Graphics&) {}
#endif

// ── Lifecycle hooks ──────────────────────────────────────────────────────────

void KromosynthAudioProcessorEditor::navigateSiteTo (const juce::String& path)
{
    if (site_ == nullptr) return;

    // Same ?client marker showSite appends, so the app still knows where it is
    // -- a deep link that lost it would render the browser presentation inside
    // the plugin.
    const juce::String url = juce::String (KROMOSYNTH_WEB_APP_URL)
                           + path
                           + (path.contains ("?") ? "&" : "?")
                           + "client=synthis-plugin&chrome=host";

    proc_.logEvent ("navigating to " + url);
    site_->goToURL (url);
    siteLoaded_ = true;
}

void KromosynthAudioProcessorEditor::showSite (bool shouldShowSite)
{
    if (site_ == nullptr || browser_ == nullptr)
        return;

    siteVisible_ = shouldShowSite;

    // While the site view is hidden no silence clock runs on the activity
    // list; showing it again restarts the clocks from zero.
    activity_.setSiteHidden (! shouldShowSite);

    // Loaded lazily, and only once. Navigating on every open would throw away
    // whatever the user had reached -- including a half-completed sign-in.
    if (shouldShowSite && ! siteLoaded_)
    {
        // ?client=synthis-plugin lets the web app know where it is without
        // sniffing the User-Agent. The embedded WKWebView UA is detectable
        // (STAGE0_FINDINGS B3), but that fingerprint also matches Electron
        // shells and in-app browsers, so the app treats it as a fallback and
        // this parameter as the statement. See packages/web/src/services/
        // pluginHost.js.
        // chrome=host as well as client=synthis-plugin, and the second one is
        // the one that was missing.
        //
        // hostDrawsChrome() in pluginHost.js:101 tests for chrome=host. The
        // plugin has only ever sent client=synthis-plugin, so App.jsx:1450's
        // `if (hostDrawsChrome()) return <NavigationBar hideNav />` has NEVER
        // FIRED and the web app has been drawing its own nav inside the plugin
        // the whole time. WP_UNIFIED_CHROME section 2 says "half of this work
        // is already done and was done deliberately" -- the code was, the
        // trigger was not.
        //
        // Two markers rather than one, deliberately: client says WHERE the app
        // is, chrome says WHAT THE HOST HAS DRAWN. pluginHost.js's own comment
        // makes the distinction -- "a fingerprint can suggest where we are, but
        // it cannot tell us what the host has drawn" -- so collapsing them
        // would lose a case, a plugin surface that draws no chrome of its own.
        const juce::String url = juce::String (KROMOSYNTH_WEB_APP_URL)
                               + "?client=synthis-plugin&chrome=host";

        proc_.logEvent ("navigating to site " + url);
        site_->goToURL (url);
        siteLoaded_ = true;
    }

    site_->setVisible (shouldShowSite);

    // THE SHELL IS NOT HIDDEN HERE, and there is no line doing it.
    //
    // That is the whole design. The nav lives in the shell's own document, so
    // hiding the shell would take the chrome with it. resized() gives it a
    // strip instead, and the page's own body.strip clears everything below its
    // nav so the strip shows a bar and not the top of a list.
    //
    // The A3.2 probe (2026-09-10) established that two WKWebViews DO composite
    // simultaneously in an AUv3. It was reverted on the day because it was not
    // a layout -- it stacked three chromes and looked it. C35 is the layout,
    // and C36 measured it in AUM.
    //
    // WHICH TAB READS AS CURRENT IS THE PAGE'S BUSINESS. It is always the
    // initiator -- showSite is reachable only from openSite, openSitePath and
    // showLibrary -- so it already knows, and there is nothing to tell it. The
    // native bar needed telling, which is why a destination index used to be
    // kept on the editor.

    proc_.logEvent (shouldShowSite ? "surface_site" : "surface_shell");

    resized();
}

bool KromosynthAudioProcessorEditor::InstrumentedBrowser::pageAboutToLoad (const juce::String& url)
{
    // Sentinel FIRST, and redacted logging with it.
    //
    // Both orderings below are load-bearing. The unconditional log line that
    // used to open this function would have written a ticket to
    // ~/Library/Logs in plaintext, and the origin check that follows would
    // have treated the sentinel as an off-origin navigation and handed it to
    // launchInDefaultBrowser -- leaking the ticket to whatever the OS opens
    // with it. Neither is recoverable after the fact.
    // AUTH AND CHECKOUT BEFORE ACQUISITION, and the order is load-bearing.
    //
    // `acquisition::isSentinel` matches ANY synthis:// URL and parseSentinel
    // then answers nullopt for an unknown verb. So an auth or checkout
    // sentinel reaching the branch below would be blocked (right) and logged
    // as `acquisition_sentinel_malformed` (wrong, and it would send the next
    // reader of the log looking for a broken download -- see
    // CheckoutSentinel.h for checkout's own version of this trap).
    if (interceptsSentinels && kromosynth::auth::isAuthSentinel (url))
    {
        if (auto s = kromosynth::auth::parse (url))
        {
            // No token in an auth sentinel, so the whole URL is loggable --
            // unlike acquisition's, which carries a ticket and needs
            // redactForLog. See the note at the top of AuthSentinel.h.
            proc_.logEvent ("auth_sentinel " + kromosynth::auth::toWire (s->kind)
                            + (s->label.isNotEmpty() ? " label=" + s->label : juce::String())
                            + (s->unread ? " unread" : ""));

            if (onAuthState != nullptr)
                onAuthState (*s);
        }
        else
        {
            proc_.logEvent ("auth_sentinel_malformed");
        }

        return false;
    }

    // PROGRESS BEFORE CHECKOUT AND ACQUISITION, same greedy-match reason as
    // auth above: `acquisition::isSentinel` matches ANY synthis:// URL, so a
    // progress URL reaching that branch would be blocked (right) and logged
    // `acquisition_sentinel_malformed` (wrong).
    if (interceptsSentinels && kromosynth::progress::isProgressSentinel (url))
    {
        if (auto ev = kromosynth::progress::parse (url))
        {
            // No credential in a progress sentinel, so the fields are
            // loggable whole — unlike acquisition's, which needs redactForLog.
            proc_.logEvent ("progress_sentinel " + ev->job + " "
                            + kromosynth::progress::toWire (ev->phase)
                            + " pct=" + juce::String (ev->pct)
                            + (ev->title.isNotEmpty() ? " title=" + ev->title : juce::String())
                            + (ev->artifact.isNotEmpty() ? " artifact=" + ev->artifact : juce::String()));

            if (onProgress != nullptr)
                onProgress (*ev);
        }
        else if (kromosynth::progress::hasUnknownPhase (url))
        {
            // A newer site may add phases; an older plugin must not misread
            // them. Blocked and logged, never let through.
            proc_.logEvent ("progress_sentinel_unknown_phase " + url);
        }
        else
        {
            // Same contract as the other verbs: isProgressSentinel said yes
            // and parse said no, so this is malformed and still blocked.
            proc_.logEvent ("progress_sentinel_malformed " + url);
        }

        return false;
    }

    if (interceptsSentinels && kromosynth::checkout::isCheckoutSentinel (url))
    {
        if (auto destination = kromosynth::checkout::parse (url))
        {
            // weak, not `this`/`&proc_` captured bare: the presentation
            // completion arrives asynchronously off UIKit's own animation,
            // and nothing stops the host from unloading this plugin instance
            // before it fires. See lifetimeToken()'s comment.
            const auto outcome = kromosynth::browser::openExternal (
                *this, juce::URL (*destination), proc_.wrapperType,
                [weak = proc_.lifetimeToken(), &proc = proc_] (bool presented)
                {
                    if (weak.expired())
                        return;

                    // Confirms the Safari sheet's presentation animation
                    // actually completed -- i.e. something real reached the
                    // screen -- rather than just that this code attempted it.
                    proc.logEvent (juce::String ("checkout_sentinel_presented success=")
                                   + (presented ? "1" : "0"));
                });

            proc_.logEvent (juce::String ("checkout_sentinel via=")
                            + (outcome == kromosynth::browser::Outcome::openedViaSafariView
                                   ? "safari_view" : "system_browser"));
        }
        else
        {
            // Same contract as the other two sentinels: isCheckoutSentinel
            // said yes and parse said no, so this is malformed and still
            // blocked -- never fall through to the generic off-origin
            // handling below, which would hand the raw synthis:// URL
            // (unparsed, unresolved) to launchInDefaultBrowser instead.
            proc_.logEvent ("checkout_sentinel_malformed");
        }

        return false;
    }

    if (interceptsSentinels && kromosynth::acquisition::isSentinel (url))
    {
        proc_.logEvent ("acquisition_sentinel "
                        + kromosynth::acquisition::redactForLog (url));

        if (auto request = kromosynth::acquisition::parseSentinel (url))
        {
            // The verb decides which of the two roots the runner uses; both are
            // handed over because this is the layer that knows where things go.
            const bool queued = proc_.acquisitions().start (
                *request,
                KROMOSYNTH_VI_SERVICE_URL,
                InstrumentLibrary::root(),
                kromosynth::renders::outbox());

            // NAMED IN THE LOG. "acquisition_queued" for a render would send the
            // next reader looking through the instrument library for a file that
            // was never going there.
            const juce::String verb =
                request->verb == kromosynth::acquisition::Verb::render ? "render" : "acquisition";

            proc_.logEvent (queued ? verb + "_queued " + request->artifactId
                                   : verb + "_already_running " + request->artifactId);
        }
        else
        {
            // isSentinel said yes and parseSentinel said no, so the site sent a
            // malformed one. Still blocked: letting it through because we could
            // not parse it is how the ticket would reach the network.
            proc_.logEvent ("acquisition_sentinel_malformed");
        }

        return false;
    }

    proc_.logEvent ("page_about_to_load " + url);

    if (allowedOrigin.isEmpty() || url.startsWith (allowedOrigin))
        return true;

    // Off-origin on a component that carries the native bridge. Blocked, and
    // opened in the system browser instead so the link still works for the
    // user without the page that receives it inheriting window.__JUCE__.
    //
    // about:blank and the resource-provider root's own sub-requests come
    // through here too; startsWith on the origin covers those.
    const auto outcome = kromosynth::browser::openExternal (*this, juce::URL (url), proc_.wrapperType);

    proc_.logEvent ("navigation_blocked " + url + " via="
                    + (outcome == kromosynth::browser::Outcome::openedViaSafariView
                           ? "safari_view" : "system_browser"));

    return false;
}

void KromosynthAudioProcessorEditor::InstrumentedBrowser::pageFinishedLoading (const juce::String& url)
{
    proc_.logEvent ("page_finished_loading " + url);

    // A full load of the site view means the document was replaced and every
    // follower on it died: every non-terminal activity entry is lost at once
    // rather than waiting out the silence lease. Set on the site component
    // only, so the shell's own loads do not fire it.
    if (onSiteReloaded != nullptr)
        onSiteReloaded();

#if KROMOSYNTH_WEBVIEW_PROBE
    // Delayed so React has actually mounted — pageFinishedLoading fires on
    // document load, not on app render, and rootChildren would always read 0.
    //
    // ---------------------------------------------------------------------
    // BUG FIX 2026-08-25. The first version of this captured a raw `this`:
    //
    //     juce::Timer::callAfterDelay (3000, [this] { evaluateJavascript(...) });
    //
    // If the editor is destroyed inside that 3-second window — closing the
    // plugin window, switching tracks, removing an instance — the timer fires
    // into freed memory. Use-after-free, in a process the host watchdogs.
    // The inner evaluateJavascript callback had the same defect: it captures
    // `this` and touches `proc_` after an asynchronous round-trip into
    // WebKit, which can easily outlive the editor.
    //
    // This is OUR bug, not a WKWebView or JUCE finding. It is exactly the
    // failure mode that would have been misattributed to the framework and
    // used to wrongly change the WP.
    //
    // Component::SafePointer nulls itself when the component is deleted.
    // ---------------------------------------------------------------------
    juce::Component::SafePointer<InstrumentedBrowser> safeThis (this);

    // Captured BY VALUE. juce::File is a path string, not a handle to a live
    // object, so the destroyed-editor branch below can log without touching
    // anything that may have been freed.
    const juce::File logFile = proc_.getLogFile();

    auto logRaw = [logFile] (const juce::String& line)
    {
        logFile.appendText (juce::Time::getCurrentTime().toISO8601 (true)
                            + "  " + line + "\n", false, false, "\n");
    };

    juce::Timer::callAfterDelay (KROMOSYNTH_PROBE_DELAY_MS, [safeThis, logRaw]() mutable
    {
        if (safeThis == nullptr)
        {
            // The guard firing is the RESULT of the lifetime test, so it must
            // be visible. Previously this returned silently, which made a
            // working guard indistinguishable from a crash: both produce "no
            // PROBE line". Now the log says which happened.
            logRaw ("probe_skipped_editor_gone (SafePointer guard held)");
            return;
        }

        safeThis->evaluateJavascript (kProbeScript,
            [safeThis, logRaw] (juce::WebBrowserComponent::EvaluationResult result) mutable
            {
                if (safeThis == nullptr)
                {
                    logRaw ("probe_result_dropped_editor_gone (SafePointer guard held)");
                    return;
                }

                if (const auto* value = result.getResult())
                    safeThis->proc_.logEvent ("PROBE " + value->toString());
                else if (const auto* error = result.getError())
                    safeThis->proc_.logEvent ("PROBE failed: " + error->message);
                else
                    safeThis->proc_.logEvent ("PROBE failed: no result, no error");
            });
    });

    // ── Sentinel rate probe (synthis-7o3.1) ──────────────────────────────────
    //
    // Answers one question for docs/RENDER_PROGRESS_DESIGN.md: does the site
    // setting `window.location.href = "synthis://progress?..."` over and over
    // disturb the page it is set from -- focus, scroll position, frame cadence,
    // a reload? Every sentinel is a cancelled navigation, so the expectation is
    // no, but "expected" is not "measured", and the progress verb will fire
    // during exactly the moments a user is typing or scrolling.
    //
    // INERT unless the environment variable is set, and only on the site view of
    // a Standalone: `KROMOSYNTH_SENTINEL_PROBE=<hz>:<seconds>`, e.g. `0.33:30`
    // for the contract rate (one event every 3 s) or `10:30` as a stress case.
    // `0:30` runs the same measurement with no sentinels, which is the baseline
    // the other runs are read against. The script and its counters are in
    // kSentinelProbeScript below; the result is logged as `SENTINEL_PROBE {...}`
    // and the sentinels themselves land in the log as `progress_sentinel ...`,
    // which is the count of events that actually reached pageAboutToLoad.
    //
    // Same SafePointer discipline as the probe above, for the same reason.
    const auto sentinelSpec = juce::SystemStats::getEnvironmentVariable ("KROMOSYNTH_SENTINEL_PROBE", {});

    if (interceptsSentinels && sentinelSpec.isNotEmpty()
        && proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        // hz:seconds[:burst[:drive]]   drive = page (default) | native
        const auto fields = juce::StringArray::fromTokens (sentinelSpec, ":", "");
        const double hz = juce::jmax (0.0, fields[0].getDoubleValue());
        const int secs = fields.size() > 1 ? juce::jlimit (5, 600, fields[1].getIntValue()) : 30;
        const int burst = fields.size() > 2 ? juce::jlimit (1, 50, fields[2].getIntValue()) : 1;
        const bool nativeDrive = fields.size() > 3 && fields[3] == "native";

        const juce::String script = juce::String (kSentinelProbeScript)
            .replace ("__HZ__", juce::String (hz, 3))
            .replace ("__SECS__", juce::String (secs))
            .replace ("__BURST__", juce::String (burst))
            .replace ("__DRIVE__", nativeDrive ? "native" : "page");

        juce::Timer::callAfterDelay (KROMOSYNTH_PROBE_DELAY_MS + 2000, [safeThis, logRaw, script, hz, secs, nativeDrive]() mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->proc_.logEvent ("sentinel_probe_start");
            safeThis->evaluateJavascript (script, [] (juce::WebBrowserComponent::EvaluationResult) {});

            if (nativeDrive && hz > 0.0)
            {
                const int periodMs = juce::jmax (1, juce::roundToInt (1000.0 / hz));
                fireSentinelProbe (safeThis.getComponent(), periodMs, secs * 1000);
            }
        });

        juce::Timer::callAfterDelay (KROMOSYNTH_PROBE_DELAY_MS + 2000 + (secs + 3) * 1000, [safeThis, logRaw]() mutable
        {
            if (safeThis == nullptr)
            {
                logRaw ("sentinel_probe_dropped_editor_gone");
                return;
            }

            // sentFinal is the live counter: the snapshot in the result is taken
            // when the window closes, and a native clock keeps firing for a few
            // calls after that, so comparing the snapshot with the log would
            // report phantom losses.
            safeThis->evaluateJavascript ("window.__sentinelProbeResult ? JSON.stringify(Object.assign("
                                          "JSON.parse(window.__sentinelProbeResult), "
                                          "{ sentFinal: window.__sentinelProbe.sent })) : 'pending'",
                [safeThis, logRaw] (juce::WebBrowserComponent::EvaluationResult result) mutable
                {
                    if (safeThis == nullptr)
                        return;

                    if (const auto* value = result.getResult())
                        safeThis->proc_.logEvent ("SENTINEL_PROBE " + value->toString());
                    else
                        safeThis->proc_.logEvent ("SENTINEL_PROBE failed");
                });
        });
    }
#endif
}

bool KromosynthAudioProcessorEditor::InstrumentedBrowser::pageLoadHadNetworkError (const juce::String& errorInfo)
{
    proc_.logEvent ("PAGE_LOAD_ERROR " + errorInfo);
    return true;   // show the default error page rather than a blank window
}

void KromosynthAudioProcessorEditor::InstrumentedBrowser::windowCloseRequest()
{
    proc_.logEvent ("window_close_request");
}

void KromosynthAudioProcessorEditor::InstrumentedBrowser::newWindowAttemptingToLoad (const juce::String& url)
{
    // Relevant to auth: WP §5 mandates system-browser + PKCE rather than an
    // embedded webview login (RFC 8252). If the app tries to open a popup in
    // here, that is the shape of the problem showing up early.
    proc_.logEvent ("new_window_attempt " + url);
}
