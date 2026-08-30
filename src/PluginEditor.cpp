// DISPOSABLE — see PluginEditor.h.

#include "PluginEditor.h"
#include "ShellPage.h"

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
            if (path != "/" && path != "/index.html")
                return std::nullopt;

            const juce::String html (kromosynth::shell::kIndexHtml);
            const auto utf8 = html.toRawUTF8();
            const auto len = std::strlen (utf8);

            std::vector<std::byte> bytes (len);
            std::memcpy (bytes.data(), utf8, len);

            return juce::WebBrowserComponent::Resource {
                std::move (bytes), juce::String ("text/html") };
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
                // Region count would mean loading every SFZ on every list, so
                // it is reported as 0 here and filled in on load. Stated
                // rather than silently omitted: the UI shows nothing instead
                // of a wrong number.
                o->setProperty ("regions", 0);
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

    browser_ = std::make_unique<InstrumentedBrowser> (options, proc_);
    browser_->allowedOrigin = juce::WebBrowserComponent::getResourceProviderRoot();
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
    addChildComponent (*site_);

    siteBar_ = std::make_unique<SiteBar> ([this] { showSite (false); });
    addChildComponent (*siteBar_);
#endif

    // Resizable with a wide range, because "does it resize" is a gate
    // question and a fixed-size editor cannot answer it. Lower bound is
    // small enough to catch layout collapse; upper bound is large enough
    // that a 4K panel is not artificially clamped.
    setResizable (true, true);
    setResizeLimits (640, 400, 3840, 2400);
    setSize (1280, 800);

#if ! KROMOSYNTH_EDITOR_STUB
    // The shell, not the site. The site is a second component with native
    // integration OFF; see WP section 12. Not built yet -- acquisition needs
    // the sentinel-URL hook on the web app side.
    const auto shellRoot = juce::WebBrowserComponent::getResourceProviderRoot();
    proc_.logEvent ("navigating to shell " + shellRoot);
    browser_->goToURL (shellRoot);
#endif
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

    if (siteBar_ != nullptr)
        siteBar_->setBounds (bounds.removeFromTop (siteVisible_ ? 32 : 0));

    if (site_ != nullptr)
        site_->setBounds (bounds);

    if (browser_ != nullptr)
    {
        // The shell keeps the full area. It is the surface the inset
        // diagnostic was measured against, and giving it a different rectangle
        // depending on whether the site strip exists would invalidate that.
        auto area = getLocalBounds();

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

KromosynthAudioProcessorEditor::SiteBar::SiteBar (std::function<void()> onBack)
{
    back.onClick = std::move (onBack);
    addAndMakeVisible (back);
}

void KromosynthAudioProcessorEditor::SiteBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (0x1a, 0x1d, 0x21));
}

void KromosynthAudioProcessorEditor::SiteBar::resized()
{
    back.setBounds (getLocalBounds().removeFromLeft (110).reduced (4));
}

void KromosynthAudioProcessorEditor::showSite (bool shouldShowSite)
{
    if (site_ == nullptr || siteBar_ == nullptr || browser_ == nullptr)
        return;

    siteVisible_ = shouldShowSite;

    // Loaded lazily, and only once. Navigating on every open would throw away
    // whatever the user had reached -- including a half-completed sign-in.
    if (shouldShowSite && ! siteLoaded_)
    {
        proc_.logEvent ("navigating to site " KROMOSYNTH_WEB_APP_URL);
        site_->goToURL (KROMOSYNTH_WEB_APP_URL);
        siteLoaded_ = true;
    }

    site_->setVisible (shouldShowSite);
    siteBar_->setVisible (shouldShowSite);
    browser_->setVisible (! shouldShowSite);

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
    if (interceptsSentinels && kromosynth::acquisition::isSentinel (url))
    {
        proc_.logEvent ("acquisition_sentinel "
                        + kromosynth::acquisition::redactForLog (url));

        if (auto request = kromosynth::acquisition::parseSentinel (url))
        {
            const bool queued = proc_.acquisitions().start (
                *request,
                KROMOSYNTH_VI_SERVICE_URL,
                InstrumentLibrary::root());

            proc_.logEvent (queued ? "acquisition_queued " + request->artifactId
                                   : "acquisition_already_running " + request->artifactId);
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
    proc_.logEvent ("navigation_blocked " + url);

    juce::URL (url).launchInDefaultBrowser();

    return false;
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
