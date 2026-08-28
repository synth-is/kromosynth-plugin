// ─────────────────────────────────────────────────────────────────────────────
// DISPOSABLE. Rewrite or delete without ceremony.
//
// Stage 0 editor: a WebBrowserComponent pointed at the deployed synth.is web
// app, plus enough instrumentation to answer the Stage 0 gate questions with
// data instead of impressions.
//
// There is no bridge class, no message protocol, no state sync. Native
// integration is deliberately OFF — see src/CMakeLists.txt for why.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

class KromosynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit KromosynthAudioProcessorEditor (KromosynthAudioProcessor&);
    ~KromosynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Subclassed only to hook the lifecycle virtuals. Every one of them feeds
    // the log file — page loads, navigations, and failures are exactly the
    // events the Stage 0 gate is asking about.
    class InstrumentedBrowser : public juce::WebBrowserComponent
    {
    public:
        InstrumentedBrowser (const Options& options, KromosynthAudioProcessor& p)
            : juce::WebBrowserComponent (options), proc_ (p) {}

        /** Restricts navigation to a single origin.

            STAGE 1 CHANGE, and the reason matters. At Stage 0 this returned
            true unconditionally, with the comment "allow everything; we want
            to see the real app". That was right for observation and is wrong
            the moment `window.__JUCE__.backend` exists on this component: an
            off-origin navigation would carry the native function surface with
            it. A sign-in redirect to a third-party identity provider is the
            obvious path, and JUCE's own documentation warns about exactly this.

            So this is now a security boundary rather than an observation
            point. Anything off-origin is blocked and handed to the system
            browser, which is also what WP section 7 Stage 1 already wanted for
            auth ("system browser + PKCE").

            Empty allowedOrigin means "allow everything", retained only for the
            site component where native integration is OFF and the plugin is
            acting as a plain browser.
        */
        juce::String allowedOrigin;

        bool pageAboutToLoad (const juce::String& url) override;
        void pageFinishedLoading (const juce::String& url) override;
        bool pageLoadHadNetworkError (const juce::String& errorInfo) override;
        void windowCloseRequest() override;
        void newWindowAttemptingToLoad (const juce::String& url) override;

    private:
        KromosynthAudioProcessor& proc_;
    };

    KromosynthAudioProcessor& proc_;
    std::unique_ptr<InstrumentedBrowser> browser_;

    // ---------------------------------------------------------------------
    // DISPOSABLE DIAGNOSTIC — delete once §A3.1c has an owner.
    //
    // Built only when KROMOSYNTH_EDITOR_STUB=1. Replaces the
    // WebBrowserComponent with a plain juce::Component while leaving
    // setResizable / setResizeLimits / setSize and all logging byte-identical,
    // so the ONLY variable is whether the editor contains a native NSView.
    //
    //   AU stub resizes in Live  -> WebBrowserComponent is the problem (upstream JUCE)
    //   AU stub still dead       -> our AU editor configuration (ours to fix)
    // ---------------------------------------------------------------------
    class StubContent : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
    };

    std::unique_ptr<StubContent> stub_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KromosynthAudioProcessorEditor)
};
