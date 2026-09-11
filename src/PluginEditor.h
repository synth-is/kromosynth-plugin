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

    /** The way back from the site, and the only place a download is visible
        while the user is on it.

        The site has no bridge, so it cannot call the plugin to return to the
        library and the plugin cannot call the site to report progress. That is
        the WP section 12 pairing working as intended, not a gap to patch: the
        bridge is off so that off-origin navigation is safe for sign-in.

        The consequence is that download status has nowhere to live except here.
        The shell's status line is hidden precisely when a download is running,
        because the user is on the site when they start one.

        Drawn by the process that actually knows. Anything the web app could
        show would only mean "a ticket was handed over" -- not that the archive
        arrived, not that it held a usable SFZ, not that it installed.
    */
    class ChromeBar : public juce::Component,
                      private juce::Timer
    {
    public:
        /** One row for every destination the bar can reach.

            `path` empty means the PLUGIN'S OWN LIBRARY, which is not a site
            page and is why the shell is called "Library" and not
            "Instruments": the web app already has an /instruments, and it is a
            different thing -- the account's rendered instruments rather than
            what is on this device. Those two are the halves
            WP_UNIFIED_CHROME section 7.2 would merge, so carrying both here is
            honest about the split rather than pretending it away.

            Labels and paths are taken from NavigationBar.jsx:77-119 so the two
            cannot drift in naming. Sound Feed's path is /network, which is that
            file's own default when no feed tab is current.
        */
        struct Destination
        {
            const char* label;
            const char* path;
        };

        static constexpr Destination kDestinations[] = {
            { "Library",     ""             },
            { "Feed",        "/network"     },
            { "Garden",      "/collection"  },
            { "Instruments", "/instruments" },
            { "Biomes",      "/tree"        },
        };

        static constexpr int kNumDestinations = 5;

        // 34 for the tabs, 16 for the status row beneath them. Section 8's
        // acceptance viewport is GarageBand's 736 x 242 letterbox, which leaves
        // 192 for content -- four rows at 44, against a criterion of at least
        // three. It passes with room, where the earlier sketch that put a
        // separate 22px status strip above a 40px bar passed by a third of a
        // row. Folding the status INTO the bar is what bought that.
        static constexpr int kTabHeight = 34;
        static constexpr int kStatusHeight = 16;
        static constexpr int kBarHeight = kTabHeight + kStatusHeight;

        /** NOTHING IS DISABLED HERE, and that is a correction.

            An earlier design greyed Biomes and locked Sound Garden. Both were
            wrong. NavigationBar.jsx has exactly one disabled branch,
            `requiresAuth && !isAuthenticated`, so Sound Garden is the only item
            that can ever dim and Biomes never does -- and /tree is already the
            one deep link the plugin ships, via the tree button on each row.

            Sound Garden is not gated here either, because this process cannot
            know: section 7.1, measured in C2, is that the AUv3's WKWebView
            storage is per-container with no way to share a WKWebsiteDataStore
            across an App Group, so the session does not cross the process
            boundary. A lock drawn from a state we cannot read would be the same
            failure as the "8 voices" badge -- an internal guess printed as a
            fact. The site knows, and shows its own sign-in.
        */
        ChromeBar (KromosynthAudioProcessor& proc,
                   std::function<void()> onShowShell,
                   std::function<void (const juce::String&)> onNavigate);

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

        /** Which destination reads as current. The editor owns the truth about
            which surface is up, so it tells the bar rather than the bar
            guessing.
        */
        void setActiveDestination (int index);

    private:
        void timerCallback() override;

        /** Renders the runner's snapshot into one line.

            `measuring` is passed in rather than read off the processor so this
            stays a pure function of what it reports. It is not optional:
            without it the bar says "ready in Library" throughout the eight to
            nine seconds the keyboard measurement holds the audio path
            suspended, which is not silence about the wait but a false claim
            about it. See AUV3_IOS_FINDINGS C21.
        */
        static juce::String describe (const std::vector<AcquisitionRunner::Status>&,
                                      bool measuring,
                                      bool& anyFailed);

        KromosynthAudioProcessor& proc_;
        juce::OwnedArray<juce::TextButton> tabs_;
        juce::TextButton panic { "Panic" };
        int active_ = 0;
        juce::String status_;
        bool failed_ = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromeBar)
    };

    std::unique_ptr<ChromeBar> chromeBar_;

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

    // Which SITE destination the bar should light when the site is up. Kept on
    // the editor rather than in the bar because the editor is what knows where
    // the site actually went -- including via deep links that never touch a
    // tab. Defaults to Feed: index 0 is the library, and showSite's own lazy
    // first load goes to the app root, which is a feed.
    int activeSiteDestination_ = 1;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KromosynthAudioProcessorEditor)
};
