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
#include "ArtifactAcquisition.h"
#include "AuthSentinel.h"
// Desktop only inside, and it declares nothing on iOS. See the header for why
// that is the platform's decision rather than ours.
#include "RenderGutter.h"
// For kStripHeight, which seeds stripHeight_ below. The page owns that number;
// this header only needs its fallback.
#include "ShellPage.h"

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
        std::function<void (const kromosynth::auth::State&)> onAuthState;

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

        /** Set on the site component only.

            When true, pageAboutToLoad checks for an acquisition sentinel BEFORE
            the origin check and before anything is logged unredacted. The order
            matters twice over: the sentinel's scheme is not the allowed origin,
            so an origin-first check would hand it to launchInDefaultBrowser and
            leak the ticket to whatever the OS opens; and the log line at the top
            of the handler would otherwise write the ticket to disk.
        */
        bool interceptsSentinels = false;

        bool pageAboutToLoad (const juce::String& url) override;
        void pageFinishedLoading (const juce::String& url) override;
        bool pageLoadHadNetworkError (const juce::String& errorInfo) override;
        void windowCloseRequest() override;
        void newWindowAttemptingToLoad (const juce::String& url) override;

    private:
        KromosynthAudioProcessor& proc_;
    };

    KromosynthAudioProcessor& proc_;

    // The shell: local, resource-provider served, native bridge ON.
    std::unique_ptr<InstrumentedBrowser> browser_;

    // The site: https://synth.is, native bridge OFF, sentinel interception ON.
    // Two components rather than one, per WP section 12: a single component
    // cannot be both trusted enough for window.__JUCE__ and open enough to
    // follow a sign-in redirect.
    std::unique_ptr<InstrumentedBrowser> site_;

    /** Swaps which surface is visible. */
    void showSite (bool shouldShowSite);

    /** Shows the site and navigates it to a site-relative path.

        Separate from showSite because that one loads lazily and ONCE, so a
        reopen keeps whatever the user had reached, including a half-completed
        sign-in. A deep link is the case where replacing that is the request.
    */
    void navigateSiteTo (const juce::String& path);

    bool siteVisible_ = false;

    /** The strip's height in logical pixels, AS REPORTED BY THE PAGE.

        Not a constant, because the page's own bar is kStripHeight plus
        env(safe-area-inset-top) and neither this process nor this platform can
        tell us the second term for the rectangle we actually occupy. Measured
        2026-09-11: 62pt in the standalone on an iPhone 17 Pro simulator, and
        0 in the AUv3 under AUM on an iPhone 13 Pro Max. BOTH ENDS MEASURED --
        the AUv3 figure was an expectation for one build and is not any more:
        `chrome_height reported=51` once, with no second report, because there
        was no inset to land. A host-framed view does not touch the top of the
        screen. 51 not 50 is the border-bottom.

        Seeded with kStripHeight so a page that never reports degrades to a
        strip that is too short -- the bug this replaces -- rather than to one
        of zero height, which would hide the chrome completely.
    */
    int stripHeight_ = kromosynth::shell::kStripHeight;

    /** Who the site last said you are.

        NOT AUTHORITATIVE, and the page is told so. This is the last thing the
        site volunteered over a sentinel; the site's own session remains the
        only source of truth. Starts `unknown` rather than `none`, because
        "signed out" is a claim and "I have not been told" is not -- a bar that
        asserts signed-out before the first sentinel arrives is lying during
        every cold start.
    */
    kromosynth::auth::State authState_;

    // WebBrowserComponent has no URL getter in JUCE 9 -- goToURL only, no
    // getCurrentURL -- so "has the site been navigated yet" is tracked here
    // rather than asked of the component.
    bool siteLoaded_ = false;

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

   #if ! JUCE_IOS
    /** The native strip a render can be dragged out of.

        NATIVE, because WebKit will not put a usable local-file reference on
        the drag pasteboard from a remote origin, and a SEPARATE RECTANGLE,
        because a native webview obliterates anything drawn over it. Both
        measured; both recorded in RenderGutter.h.

        Desktop only: performExternalDragDropOfFiles is `jassertfalse` on iOS,
        where the delivery route is the Files placement instead.
    */
    std::unique_ptr<RenderGutter> gutter_;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KromosynthAudioProcessorEditor)
};
