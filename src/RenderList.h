// RenderList — the native half of the Library's Sounds sub-tab.
//
// Replaces RenderGutter, which was a full-width strip of chips along the bottom
// of the editor, always present, showing a capped subset of the same folder the
// page was also listing. Björn, on that: "I also wonder about if this is the
// best UI or UX presentation to have this persistent bottom bar with swatches
// you, for example, cannot preview just with some IDs, maybe not too user
// friendly." Both halves of that were true.
//
// ── Why this is native when the instrument list beside it is HTML ───────────
//
// Because it has to be DRAGGABLE, and an HTML row cannot be. C43 measured it:
// WebKit writes a page-initiated drag to the pasteboard as `public.url`, never
// `public.file-url`, and will not even begin a drag for a local file from a
// non-file origin. Finder produced a `.fileloc` stub; Ableton took nothing.
// That is the security boundary, not a limitation to work around.
//
// C45 then established that a native handle CAN float over the webview in its
// own window and that a drag started there does reach the host -- and found the
// reason not to: a host that narrows below the editor's minimum size clips the
// editor without telling JUCE, so a floating handle lands outside the visible
// window with no fix available from inside JUCE.
//
// So this is native and NON-OVERLAPPING. The shell webview is already a strip
// whose height the page reports plus a body, and showSite already shrinks it to
// the strip and lets something else fill the body. This is that mechanism with
// a component instead of a second webview: same window, no polling, no focus
// guard, no clipping orphan, and zero height on every other tab.
//
// ── Why playback goes back through the page ─────────────────────────────────
//
// A native list would otherwise need its own AudioDeviceManager: routing a
// preview through the plugin's output would drop it into the host's signal
// chain, and get it recorded if the transport is rolling. Instead onPlayRequest
// hands a file name to the editor, which passes it to the page, which plays
// /render/<file>.wav with WebAudio -- reaching the system output exactly as the
// site's feed play button already does inside the plugin.
//
// Native for drag, page for playback. Neither doing the other's job.
//
// ── Rows are painted, not Components ───────────────────────────────────────
//
// Play and Delete are drawn rectangles with hit-testing, as the gutter's chips
// were. A list of real Buttons would mean two child Components per row, created
// and destroyed as the folder changes, for controls that never need focus or
// keyboard handling. The cost is that they do not get JUCE's hover animation
// for free, which is why there is an explicit hoveredRow_.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if ! JUCE_IOS

#include "RenderOutbox.h"

#include <functional>
#include <vector>

class RenderList final : public juce::Component,
                         private juce::Timer
{
public:
    RenderList();
    ~RenderList() override;

    /** Log sink, injected so this class needs no reference to the processor. */
    std::function<void (const juce::String&)> onLog;

    /** Asks the page to play a delivered render, by file name.
        See the header note on why playback does not happen here.
    */
    std::function<void (const juce::String&)> onPlayRequest;

    /** The editor needs to re-lay out when wantsHandle() changes -- a dismiss,
        or a take arriving while the band is hidden. Separate from
        onContentsChanged because that one is about the page's badge.
    */
    std::function<void()> onHandleWantedChanged;

    /** The page owns the count shown in the Sounds sub-tab badge, so it has to
        hear when the folder changed underneath it -- a delete here, or a take
        landing while the list is open.
    */
    std::function<void()> onContentsChanged;

    /** Height of the one-row handle. The editor reserves exactly this. */
    static constexpr int kCompactH = 42;

    /** Whether the one-row handle is worth any height right now.

        False before the first take, and false once dismissed -- until the next
        take arrives, which un-dismisses it. Björn asked "will that bar always
        be present, not just when i have rendered a new sound?", and it was:
        anyone with takes in the folder had it on screen for every browsing
        session forever. The handle is for the sound you JUST took; the Sounds
        tab is for history, and a permanent 42pt band to say so is too much.

        Dismissal is a user action rather than a timer. A time window would have
        been a magic number nobody could predict, and the state it hides is not
        urgent enough to expire on its own.
    */
    bool wantsHandle() const { return ! rows_.empty() && ! dismissed_; }

    /** One row (the newest) instead of the whole list.

        THE ANSWER TO A GAP I MADE. Deleting the old full-width gutter left no
        way to drag a sound you had just taken while still browsing the Feed or
        the Garden -- you had to navigate to Library > Sounds first, which is
        friction in the commonest case of all: take a sound, put it on the
        timeline.

        The same component rather than a second one, so the drag, the grip, the
        cursor and the outbox reading are shared. A separate handle class would
        have been a second reader of the same folder, which RenderOutbox::list
        exists to prevent.
    */
    void setCompact (bool shouldBeCompact);

    /** Re-reads the outbox. Called on construction, after a delete, and by the
        timer when the outbox stamp moves.
    */
    void refresh();

    /** The page reports that playback stopped or finished, so the row's Play
        button can stop claiming otherwise. Empty means nothing is playing.
    */
    void setPlayingFile (const juce::String& fileName);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr int kRowH     = 34;
    static constexpr int kRowGap   = 4;
    static constexpr int kPad      = 8;
    static constexpr int kBtnW     = 54;
    static constexpr int kBtnH     = 22;
    static constexpr int kDragSlop = 6;

    struct Row
    {
        kromosynth::renders::Entry entry;
        juce::Rectangle<int> bounds;   ///< in list space, before scrolling
        juce::Rectangle<int> play;
        juce::Rectangle<int> del;
        juce::Rectangle<int> dismiss;   ///< compact mode only; empty otherwise
    };

    void timerCallback() override;
    void layOutRows();
    int rowAt (juce::Point<int> p) const;
    void say (const juce::String& line) const;
    void deleteRow (int index);
    void clampScroll();

    std::vector<Row> rows_;
    juce::String playingFile_;
    bool compact_ = false;
    bool dismissed_ = false;

    int scrollY_ = 0;
    int hoveredRow_ = -1;
    bool dragging_ = false;

    /** The outbox's state as one number: the newest wav's mtime, mixed with the
        wav COUNT. Björn's, carried over from RenderGutter, and the reasoning is
        his: a directory's mtime moves when Ableton writes its own `.asd`
        sidecar next to a file we just handed it, so stamping the directory made
        every successful drag trigger a rescan two seconds later. Counting as
        well as timing catches a deletion, which can leave an older
        newest-mtime that a timestamp alone reads as "nothing happened".
    */
    juce::int64 stamp_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderList)
};

#endif // ! JUCE_IOS
