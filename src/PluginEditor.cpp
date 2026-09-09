// DISPOSABLE — see PluginEditor.h.

#include "PluginEditor.h"
#include "ShellPage.h"

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
            // The QUERY IS STRIPPED before matching, because the shell is
            // loaded with a cache-busting one appended -- see where goToURL is
            // called. Matching the raw path would 404 the page the moment that
            // query existed.
            const auto clean = path.upToFirstOccurrenceOf ("?", false, false);

            if (clean != "/" && clean != "/index.html")
                return std::nullopt;

            const juce::String html (kromosynth::shell::kIndexHtml);
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
                o->setProperty ("kind", e.kind == InstrumentLibrary::Kind::faust
                                            ? "faust" : "sampler");
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
    addChildComponent (*site_);

    siteBar_ = std::make_unique<SiteBar> (proc_, [this] { showSite (false); });
    addChildComponent (*siteBar_);
#endif

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
    setResizeLimits (640, 400, 3840, 2400);
    setSize (1280, 800);
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

KromosynthAudioProcessorEditor::SiteBar::SiteBar (KromosynthAudioProcessor& proc,
                                                  std::function<void()> onBack)
    : proc_ (proc)
{
    back.onClick = std::move (onBack);
    addAndMakeVisible (back);
}

void KromosynthAudioProcessorEditor::SiteBar::visibilityChanged()
{
    // Only poll while the user can see the result. While the site is hidden the
    // shell is up and doing its own polling, and two timers asking the same
    // question is one too many.
    if (isVisible())
    {
        timerCallback();      // paint now rather than after one tick
        startTimer (250);
    }
    else
    {
        stopTimer();
    }
}

juce::String KromosynthAudioProcessorEditor::SiteBar::describe (
    const std::vector<AcquisitionRunner::Status>& jobs, bool measuring, bool& anyFailed)
{
    anyFailed = false;

    int active = 0;
    double worstProgress = 1.0;

    for (const auto& j : jobs)
    {
        if (j.finished)
            continue;

        ++active;

        if (j.progress >= 0.0)
            worstProgress = juce::jmin (worstProgress, j.progress);
    }

    if (active > 0)
    {
        // No artefact id in the text. It is a UUID, it means nothing to the
        // person reading it, and it is the widest thing that could go into a
        // strip that also has to hold a button.
        juce::String what = active == 1
            ? juce::String ("Downloading instrument")
            : "Downloading " + juce::String (active) + " instruments";

        if (worstProgress < 1.0)
            what << "  " << juce::roundToInt (worstProgress * 100.0) << "%";

        return what;
    }

    // Nothing running. Report failures in preference to successes: a silent
    // failure is the case where the user returns to the library, finds nothing
    // new, and has no way to find out why.
    for (const auto& j : jobs)
    {
        if (! j.ok)
        {
            anyFailed = true;
            return j.message;
        }
    }

    // MEASURING, and ahead of "ready" because "ready" is FALSE while it runs.
    //
    // C21. An acquired instrument autoloads in about half a second and the
    // audio path is then suspended for as long as the keyboard measurement
    // takes -- 8.9 s on an A15 under wasm3, against the "one to three seconds"
    // the barrier's own comment was written for. This bar said "Installed -
    // ready in Library" for every one of those seconds, so the user taps the
    // instrument, finds the measurement cached and instant, and concludes the
    // tap is what activated it. It is not, and the bar taught them that.
    //
    // Same failure as C20's badges: an internal state printed as a user-facing
    // fact. Saying so costs nothing and is strictly more true than "ready".
    //
    // The wait itself is ARCHITECTURAL and this does not shorten it. Not
    // suspending would open the barrier while the measuring render still owns
    // the engine, which is the race that produced a raw peak of 23182060. This
    // makes the wait legible and claims nothing about its length.
    //
    // BEFORE the empty-jobs case below, because the shell clears finished
    // acquisitions and can do it while the site is still open -- leaving a
    // measurement running with no job left to describe it.
    if (measuring)
        return "Measuring levels across the keyboard - a few seconds";

    if (jobs.empty())
        return {};

    return jobs.size() == 1
         ? juce::String ("Installed - ready in Library")
         : "Installed " + juce::String ((int) jobs.size()) + " instruments";
}

void KromosynthAudioProcessorEditor::SiteBar::timerCallback()
{
    bool failed = false;
    const auto text = describe (proc_.acquisitions().snapshot(),
                                proc_.isMeasuring(), failed);

    // Repaint only on change. This runs four times a second for as long as the
    // site is open, and a webview is already the expensive thing on screen.
    if (text == status_ && failed == failed_)
        return;

    status_ = text;
    failed_ = failed;
    repaint();
}

// Deliberately does NOT call clearFinished(). The shell does that, and exactly
// one consumer may: if both cleared, whichever surface polled first would
// swallow the outcome and the other would never show it. Leaving finished
// entries in place means this bar keeps reporting the last result until the
// user returns to the library, which is the moment it stops being useful.
void KromosynthAudioProcessorEditor::SiteBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (0x1a, 0x1d, 0x21));

    if (status_.isEmpty())
        return;

    g.setColour (failed_ ? juce::Colour::fromRGB (0xe0, 0x6c, 0x60)
                         : juce::Colour::fromRGB (0x9a, 0xa4, 0xae));
    g.setFont (juce::FontOptions (12.0f));

    auto area = getLocalBounds().withTrimmedLeft (120).reduced (6, 0);
    g.drawText (status_, area, juce::Justification::centredLeft, true);
}

void KromosynthAudioProcessorEditor::SiteBar::resized()
{
    back.setBounds (getLocalBounds().removeFromLeft (110).reduced (4));
}

void KromosynthAudioProcessorEditor::navigateSiteTo (const juce::String& path)
{
    if (site_ == nullptr) return;

    // Same ?client marker showSite appends, so the app still knows where it is
    // -- a deep link that lost it would render the browser presentation inside
    // the plugin.
    const juce::String url = juce::String (KROMOSYNTH_WEB_APP_URL)
                           + path
                           + (path.contains ("?") ? "&" : "?")
                           + "client=synthis-plugin";

    proc_.logEvent ("navigating to " + url);
    site_->goToURL (url);
    siteLoaded_ = true;
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
        // ?client=synthis-plugin lets the web app know where it is without
        // sniffing the User-Agent. The embedded WKWebView UA is detectable
        // (STAGE0_FINDINGS B3), but that fingerprint also matches Electron
        // shells and in-app browsers, so the app treats it as a fallback and
        // this parameter as the statement. See packages/web/src/services/
        // pluginHost.js.
        const juce::String url = juce::String (KROMOSYNTH_WEB_APP_URL)
                               + "?client=synthis-plugin";

        proc_.logEvent ("navigating to site " + url);
        site_->goToURL (url);
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
