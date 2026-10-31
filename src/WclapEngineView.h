// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Björn Þór Jónsson
// WclapEngineView — shows a hosted WCLAP's OWN editor page inside the plugin editor (synthis-et4.4).
//
// The page is the bundle's ui/ (for Surge XT: a 20 MB ui.wasm that draws the real Surge editor into a
// canvas). It runs in a juce::WebBrowserComponent (WKWebView on macOS) whose resource provider serves the
// bundle's files, and talks to the engine through clap.webview, carried by WclapEngine::guiOpen /
// guiReceive. Nothing in the bundle and nothing in the bridge is changed: the page speaks
// `window.parent.postMessage(ArrayBuffer)` out and `message` events in, and in a top-level webview
// window.parent IS window, so a small script injected before the page (kShim) is the whole adapter:
//
//   page -> engine   shim catches postMessage(ArrayBuffer) -> JUCE event "wclapFrame" {b: base64} -> guiReceive
//   engine -> page   PageSink queues the bytes -> evaluateJavascript(__wclapDeliver([base64...]))
//                    -> MessageEvent{data: ArrayBuffer, source: window}   (webclap-ui.js drops events whose
//                    source is not window.parent, so `source` must be set; the bridge's own webview-gui
//                    forgets to and would lose every engine->page frame)
//
// DISPOSABLE in the way WclapEngine is: when a second hosted engine exists this wants a common shape.
// Compiled only with KROMOSYNTH_HAS_WCLAP.

#pragma once

#if KROMOSYNTH_HAS_WCLAP

#include "WclapEngine.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>

class WclapEngineView : public juce::Component,
                        private juce::AsyncUpdater
{
public:
    /** Opens the engine's gui and starts loading its page. `ok()` says whether that worked. */
    explicit WclapEngineView (WclapEngine& engine, std::function<void (const juce::String&)> log);
    ~WclapEngineView() override;

    bool ok() const noexcept { return ok_; }
    juce::String problem() const { return problem_; }
    int generation() const noexcept { return generation_; }
    /** The editor size the guest asked for, logical pixels (0 when it did not say). */
    juce::Point<int> preferredSize() const noexcept
    {
        return pageSize_.x > 0 ? pageSize_ : juce::Point<int> { (int) gui_.width, (int) gui_.height };
    }

    void resized() override;

    /** The page's own size when it has said (protocol "resize"), else zero; and the editor's hook to fit it. */
    juce::Point<int> pageSize() const noexcept { return pageSize_; }
    std::function<void (juce::Point<int>)> onPageSize;

    /** Runs a script in the page and hands back its string result ("error" if it threw). Probe / diagnostics. */
    void evaluate (const juce::String& js, std::function<void (const juce::String&)> done);

    // ── pure pieces, public so the probe can test them without a window ─────────────────────────────
    /** The file a request path maps to inside the bundle, or an empty File if it is outside ui/. */
    static juce::File resolveRequest (const juce::File& bundleDir, const juce::String& path);
    static std::optional<juce::WebBrowserComponent::Resource> serve (const juce::File& bundleDir,
                                                                    const juce::String& path);
    static juce::String mimeFor (const juce::File& f);
    static const char* shimScript();

    /** Measurements, filled in as the page reports (message thread). -1 until known. */
    struct Timings
    {
        double openToLoadedMs = -1;   ///< component created -> pageFinishedLoading
        double openToReadyMs = -1;    ///< -> the UI module is up and drawing (webclapDebug "ready")
        double uiInitMs = -1;         ///< ui.wasm's own init, as the page measured it
        int pageWidth = 0, pageHeight = 0;
        int framesToPage = 0, framesFromPage = 0, paramFramesFromPage = 0, gestureFramesFromPage = 0;
        double resourceBytes = 0, resourceMs = 0;   ///< what the provider read, and how long that took
    };
    const Timings& timings() const noexcept { return timings_; }

private:
    class Browser;

    void handleAsyncUpdate() override;
    void pageFinished();

    WclapEngine& engine_;
    std::function<void (const juce::String&)> log_;
    WclapEngine::GuiOpenResult gui_;
    bool ok_ = false;
    juce::String problem_;
    int generation_ = 0;

    juce::SpinLock queueLock_;
    std::vector<juce::String> outbound_;   // base64 frames engine -> page, waiting for the message thread
    bool pageLoaded_ = false;
    juce::Point<int> pageSize_;

    double openedAt_ = 0;
    Timings timings_;
    mutable juce::SpinLock timingLock_;

    std::unique_ptr<Browser> browser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WclapEngineView)
};

#endif
