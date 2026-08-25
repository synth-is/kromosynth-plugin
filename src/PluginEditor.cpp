// DISPOSABLE — see PluginEditor.h.

#include "PluginEditor.h"

#ifndef KROMOSYNTH_WEB_APP_URL
  #define KROMOSYNTH_WEB_APP_URL "https://synth.is"
#endif
#ifndef KROMOSYNTH_WEBVIEW_PROBE
  #define KROMOSYNTH_WEBVIEW_PROBE 1
#endif
#ifndef KROMOSYNTH_PROBE_DELAY_MS
  #define KROMOSYNTH_PROBE_DELAY_MS 3000
#endif

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

    using Options = juce::WebBrowserComponent::Options;

    auto options = Options{}
        // Without this the page is torn down and reloaded every time the host
        // hides the editor, which would make "does it survive close/reopen"
        // a question about JUCE's default rather than about the host.
        .withKeepPageLoadedWhenBrowserIsHidden();

    browser_ = std::make_unique<InstrumentedBrowser> (options, proc_);
    addAndMakeVisible (*browser_);

    // Resizable with a wide range, because "does it resize" is a gate
    // question and a fixed-size editor cannot answer it. Lower bound is
    // small enough to catch layout collapse; upper bound is large enough
    // that a 4K panel is not artificially clamped.
    setResizable (true, true);
    setResizeLimits (640, 400, 3840, 2400);
    setSize (1280, 800);

    proc_.logEvent ("navigating to " KROMOSYNTH_WEB_APP_URL);
    browser_->goToURL (KROMOSYNTH_WEB_APP_URL);
}

KromosynthAudioProcessorEditor::~KromosynthAudioProcessorEditor()
{
    proc_.logEvent ("editor_destroyed");
}

void KromosynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Visible only in the moment before the page paints, and behind it if the
    // webview fails to create at all — in which case a plain dark rectangle
    // with nothing in it is itself the finding.
    g.fillAll (juce::Colour::fromRGB (0x10, 0x12, 0x14));
}

void KromosynthAudioProcessorEditor::resized()
{
    if (browser_ != nullptr)
        browser_->setBounds (getLocalBounds());

    proc_.logEvent ("editor_resized w=" + juce::String (getWidth())
                    + " h=" + juce::String (getHeight()));
}

// ── Lifecycle hooks ──────────────────────────────────────────────────────────

bool KromosynthAudioProcessorEditor::InstrumentedBrowser::pageAboutToLoad (const juce::String& url)
{
    proc_.logEvent ("page_about_to_load " + url);
    return true;   // allow everything at Stage 0; we want to see the real app
}

void KromosynthAudioProcessorEditor::InstrumentedBrowser::pageFinishedLoading (const juce::String& url)
{
    proc_.logEvent ("page_finished_loading " + url);

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
