// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// See WclapEngineView.h. synthis-et4.4.

#include "WclapEngineView.h"

#if KROMOSYNTH_HAS_WCLAP

namespace
{
// Runs after JUCE's backend definitions and before any page script (Options::withUserScript).
//
// * window.parent === window in a top-level webview, so the page's `window.parent.postMessage(buf, "*", [buf])`
//   is window.postMessage. Binary frames go to the engine; the JSON debug objects the bundle's page posts
//   ({webclapDebug: "ready"|"stats"|"error"}) are forwarded to the log (stats as a once-a-second paint summary);
//   {webclapWheel} is for hosts that scroll a surrounding view, there is none, drop it.
// * inbound: a MessageEvent whose `source` is window, because webclap-ui.js ignores any other.
// * base64 both ways: JUCE's event channel carries JSON, not bytes.
constexpr const char* kShim = R"JS(
(function () {
  if (window.__wclapShim) return;
  window.__wclapShim = true;
  const emit = (id, o) => window.__JUCE__.backend.emitEvent(id, o);
  const toB64 = (v) => {
    const u = v instanceof ArrayBuffer ? new Uint8Array(v) : new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
    let s = "";
    for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000));
    return btoa(s);
  };
  const real = window.postMessage.bind(window);
  window.postMessage = function (data, target, transfer) {
    if (data instanceof ArrayBuffer || ArrayBuffer.isView(data)) { emit("wclapFrame", { b: toB64(data) }); return; }
    if (data && data.webclapDebug === "stats") {   // ~60 per second: forward a once-a-second summary instead
      const st = window.__wclapStats = window.__wclapStats || { frames: 0, painted: 0, pixels: 0, last: performance.now() };
      st.frames++; if (data.stats && data.stats.rects > 0) { st.painted++; st.pixels += data.stats.pixels; }
      if (performance.now() - st.last > 1000) {
        emit("wclapDebug", { json: JSON.stringify({ webclapDebug: "paint", frames: st.frames, framesWithPixels: st.painted, pixels: st.pixels }) });
        st.last = performance.now();
      }
    } else if (data && data.webclapDebug) emit("wclapDebug", { json: JSON.stringify(data) });
    if (data && (data.webclapDebug || data.webclapWheel)) return;
    return real(data, target, transfer);
  };
  window.__wclapDeliver = function (frames) {
    for (const b of frames) {
      const bin = atob(b), u = new Uint8Array(bin.length);
      for (let i = 0; i < u.length; i++) u[i] = bin.charCodeAt(i);
      window.dispatchEvent(new MessageEvent("message", { data: u.buffer, source: window }));
    }
  };
})();
)JS";
}

const char* WclapEngineView::shimScript() { return kShim; }

juce::String WclapEngineView::mimeFor (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    if (e == ".html") return "text/html";
    if (e == ".js" || e == ".mjs") return "text/javascript";
    if (e == ".wasm") return "application/wasm";
    if (e == ".json") return "application/json";
    if (e == ".css") return "text/css";
    if (e == ".xml" || e == ".svg") return e == ".svg" ? "image/svg+xml" : "text/xml";
    if (e == ".png") return "image/png";
    if (e == ".wav") return "audio/wav";
    if (e == ".txt") return "text/plain";
    return "application/octet-stream";
}

juce::File WclapEngineView::resolveRequest (const juce::File& bundleDir, const juce::String& rawPath)
{
    // Drop query and fragment, decode %xx (a factory preset name has spaces), then confine to <bundle>/ui/.
    auto path = rawPath.upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    path = juce::URL::removeEscapeChars (path);
    while (path.startsWithChar ('/')) path = path.substring (1);
    if (path.isEmpty()) path = "ui/index.html";

    const auto uiRoot = bundleDir.getChildFile ("ui");
    const auto f = bundleDir.getChildFile (path);
    if (path.contains ("..") || ! f.isAChildOf (uiRoot) || f.isDirectory()) return {};
    return f;
}

std::optional<juce::WebBrowserComponent::Resource> WclapEngineView::serve (const juce::File& bundleDir,
                                                                          const juce::String& path)
{
    const auto f = resolveRequest (bundleDir, path);
    if (f == juce::File() || ! f.existsAsFile()) return std::nullopt;

    juce::MemoryBlock bytes;
    if (! f.loadFileAsData (bytes)) return std::nullopt;

    juce::WebBrowserComponent::Resource r;
    r.mimeType = mimeFor (f);
    r.data.resize (bytes.getSize());
    std::memcpy (r.data.data(), bytes.getData(), bytes.getSize());
    return r;
}

//==============================================================================
class WclapEngineView::Browser : public juce::WebBrowserComponent
{
public:
    Browser (const Options& o, WclapEngineView& owner) : juce::WebBrowserComponent (o), owner_ (owner) {}

    void pageFinishedLoading (const juce::String&) override { owner_.pageFinished(); }

    // Everything the page can reach is inside the bundle's ui/ (the provider's scheme); nothing else loads.
    bool pageAboutToLoad (const juce::String& url) override
    {
        return url.startsWith (getResourceProviderRoot()) || url.startsWith ("about:");
    }

    void newWindowAttemptingToLoad (const juce::String&) override {}

private:
    WclapEngineView& owner_;
};

WclapEngineView::WclapEngineView (WclapEngine& engine, std::function<void (const juce::String&)> log)
    : engine_ (engine), log_ (std::move (log))
{
    openedAt_ = juce::Time::getMillisecondCounterHiRes();
    generation_ = engine.generation();

    // Engine -> page: may be called from the audio thread, so only queue.
    gui_ = engine.guiOpen ([this] (const void* data, size_t size)
    {
        {
            const juce::SpinLock::ScopedLockType l (queueLock_);
            outbound_.push_back (juce::Base64::toBase64 (data, size));
            ++timings_.framesToPage;
        }
        triggerAsyncUpdate();
    });

    if (! gui_.ok)
    {
        problem_ = gui_.detail;
        log_ ("wclap_ui_open_failed " + problem_);
        return;
    }

    const auto bundle = engine.bundleDir();

    auto options = juce::WebBrowserComponent::Options{}
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withNativeIntegrationEnabled()
        .withUserScript (kShim)
        .withEventListener ("wclapFrame", [this] (const juce::var& v)
        {
            const auto b64 = v["b"].toString();
            juce::MemoryOutputStream out;
            if (! juce::Base64::convertFromBase64 (out, b64) || out.getDataSize() == 0) return;
            ++timings_.framesFromPage;
            const auto* bytes = static_cast<const unsigned char*> (out.getData());
            if (bytes[0] == 3) ++timings_.paramFramesFromPage;
            if (bytes[0] == 4) ++timings_.gestureFramesFromPage;
            if (timings_.framesFromPage <= 8)
                log_ ("wclap_ui_frame_from_page type=" + juce::String ((int) bytes[0]) + " bytes=" + juce::String ((juce::int64) out.getDataSize()));
            // protocol "resize" (type 7): the page's real logical size. The guest's own get_size said 904x569
            // for Surge XT while its page draws 1130x711, so this, not get_size, is what the editor must fit.
            if (bytes[0] == 7 && out.getDataSize() >= 12)
            {
                juce::uint32 w = 0, h = 0;
                std::memcpy (&w, bytes + 4, 4);
                std::memcpy (&h, bytes + 8, 4);
                pageSize_ = { (int) w, (int) h };
                if (onPageSize) onPageSize (pageSize_);
            }
            engine_.guiReceive (out.getData(), out.getDataSize());
            engine_.guiService();
        })
        .withEventListener ("wclapDebug", [this] (const juce::var& v)
        {
            const auto j = juce::JSON::parse (v["json"].toString());
            const auto kind = j["webclapDebug"].toString();
            const auto since = juce::Time::getMillisecondCounterHiRes() - openedAt_;
            if (kind == "ready")
            {
                const juce::SpinLock::ScopedLockType l (timingLock_);
                timings_.openToReadyMs = since;
                timings_.uiInitMs = (double) j["initMs"];
                timings_.pageWidth = (int) j["width"];
                timings_.pageHeight = (int) j["height"];
            }
            log_ ("wclap_ui_page " + juce::JSON::toString (j, true) + " sinceOpenMs=" + juce::String (since, 0));
        })
        .withResourceProvider ([this, bundle] (const juce::String& path) -> std::optional<juce::WebBrowserComponent::Resource>
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto r = serve (bundle, path);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            if (r.has_value())
            {
                {
                    const juce::SpinLock::ScopedLockType l (timingLock_);
                    timings_.resourceBytes += (double) r->data.size();
                    timings_.resourceMs += ms;
                }
                if (r->data.size() > (1u << 20))
                    log_ ("wclap_ui_resource path=" + path + " bytes=" + juce::String ((juce::int64) r->data.size())
                          + " readMs=" + juce::String (ms, 1));
            }
            else
                log_ ("wclap_ui_resource_404 path=" + path);
            return r;
        });

    browser_ = std::make_unique<Browser> (options, *this);
    addAndMakeVisible (*browser_);

    // The guest names its own start page ("/ui/index.html"); the provider root ends in a slash.
    browser_->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + gui_.uri.trimCharactersAtStart ("/"));
    ok_ = true;
    log_ ("wclap_ui_open uri=" + gui_.uri + " guestSize=" + juce::String (gui_.width) + "x" + juce::String (gui_.height));
}

WclapEngineView::~WclapEngineView()
{
    cancelPendingUpdate();
    browser_.reset();
    engine_.guiClose();   // after the browser: no event listener can reach the engine any more
}

void WclapEngineView::resized()
{
    if (browser_ != nullptr) browser_->setBounds (getLocalBounds());
}

void WclapEngineView::evaluate (const juce::String& js, std::function<void (const juce::String&)> done)
{
    if (browser_ == nullptr) { done ("{}"); return; }
    browser_->evaluateJavascript (js, [done] (juce::WebBrowserComponent::EvaluationResult r)
    {
        if (auto* v = r.getResult()) done (v->toString());
        else done ("error");
    });
}

void WclapEngineView::pageFinished()
{
    pageLoaded_ = true;
    {
        const juce::SpinLock::ScopedLockType l (timingLock_);
        timings_.openToLoadedMs = juce::Time::getMillisecondCounterHiRes() - openedAt_;
    }
    log_ ("wclap_ui_page_loaded sinceOpenMs=" + juce::String (timings_.openToLoadedMs, 0));
    triggerAsyncUpdate();
}

void WclapEngineView::handleAsyncUpdate()
{
    engine_.guiService();
    if (! pageLoaded_ || browser_ == nullptr) return;   // frames wait: the page's listener is not there yet

    std::vector<juce::String> frames;
    {
        const juce::SpinLock::ScopedLockType l (queueLock_);
        frames.swap (outbound_);
    }
    if (frames.empty()) return;

    juce::String js = "window.__wclapDeliver([";
    for (int i = 0; i < (int) frames.size(); ++i)
        js << (i > 0 ? "," : "") << '"' << frames[(size_t) i] << '"';
    js << "])";
    browser_->evaluateJavascript (js);
}

#endif
