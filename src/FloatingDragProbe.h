// FloatingDragProbe — CAN A NATIVE COMPONENT FLOAT OVER THE WEBVIEW, and does
// it survive being used?
//
// THROWAWAY. Behind KROMOSYNTH_FLOAT_PROBE, default OFF, so it cannot ship by
// accident. Delete the file and the option together once the question is
// answered either way.
//
// ── The question ────────────────────────────────────────────────────────────
//
// Everything about delivering a render out of this plugin runs into one wall,
// stated by JUCE itself in juce_NSViewComponent.h:
//
//     since the view is a native object, it'll obliterate any JUCE components
//     that may overlap this component, but that's life
//
// So a drag handle cannot be drawn over the browser, and the only place a
// native control can live is a rectangle no webview occupies -- which forces a
// full-width band across the bottom, because a webview is a single rectangle
// and cannot have a corner cut out of it.
//
// UNLESS the handle is in its OWN WINDOW. A separate NSWindow composites above
// the webview rather than under it. If that works, Björn's corner-panel idea is
// available exactly as he described it, the webview keeps its whole rectangle,
// and -- the bigger prize -- a native LIST could overlay the page rather than
// replace it, which is what would let any historical take be dragged directly
// instead of selected first.
//
// And C43 already closed the alternative: WebKit writes public.url and never
// public.file-url, so no amount of HTML makes those rows draggable. This is the
// only open route.
//
// ── What would make it unusable ─────────────────────────────────────────────
//
// Not whether it draws. Floating windows inside plugin editors fail in service:
//
//   1. the host window MOVES and the probe detaches
//   2. the editor RESIZES and the probe sits in the wrong place
//   3. another app comes forward and the probe floats above IT -- disqualifying
//   4. a SECOND plugin instance opens and there are two, or one confused one
//
// (3) is the one that decides it, and the guard below is the honest attempt at
// it rather than a claim that it is solved: isForegroundProcess() plus the
// editor actually showing. Whether that is sufficient across hosts, spaces,
// fullscreen and multiple displays is precisely what this probe is for.
//
// Position is tracked by ComponentMovementWatcher, which is how JUCE keeps its
// OWN NSViewComponents glued to a window -- so the handle should track as
// tightly as the webview beside it does.
//
// It started on a 20 Hz timer, on the reasoning that a plugin editor is
// reparented into a host window it does not own and does not reliably hear
// about being dragged around the screen. Björn moved the Reaper window quickly
// and reported the swatch lagging behind and then catching up: 50 ms of
// visible drift, which is what polling buys you. The watcher listens to the
// whole parent chain including peer changes, which is the case the timer was
// hedging against, so the hedge was unnecessary AND worse.
//
// A slow timer remains for the two things no callback reports: the process
// coming to or leaving the foreground, and a new take appearing in the outbox.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if KROMOSYNTH_FLOAT_PROBE && ! JUCE_IOS

#include "RenderOutbox.h"

class FloatingDragProbe final : public juce::Component,
                                private juce::ComponentMovementWatcher,
                                private juce::Timer
{
public:
    /** @param anchor  the editor to track. Held by reference: this probe is
                       owned by that editor and cannot outlive it.
        @param log     where to report, so the probe needs no processor.
    */
    FloatingDragProbe (juce::Component& anchor, std::function<void (const juce::String&)> log)
        : juce::ComponentMovementWatcher (&anchor),
          anchor_ (anchor), log_ (std::move (log))
    {
        setSize (kWidth, kHeight);

        // windowIsTemporary: a utility window, no titlebar, not in the window
        // menu. Nothing about this should look like a document.
        setAlwaysOnTop (true);
        addToDesktop (juce::ComponentPeer::windowIsTemporary);

        // 4 Hz, not 20. Position is the watcher's job now; this only notices
        // the process changing foreground state and a new take landing, both of
        // which are human-speed events.
        startTimerHz (4);

        // updateVisibility, not follow: a fresh Component is invisible until
        // told otherwise and follow() declines to move a hidden window, so
        // calling it here did nothing and the handle only appeared on the first
        // tick a quarter of a second later.
        updateVisibility();

        say ("float_probe_created");
    }

    ~FloatingDragProbe() override
    {
        stopTimer();
        removeFromDesktop();
    }

    void paint (juce::Graphics& g) override
    {
        // Deliberately garish. This is a probe, and a subtle one would leave
        // "is it behind the webview or just dark?" as a question.
        g.fillAll (juce::Colour (0xffff7043));

        g.setColour (juce::Colours::black);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label_.isEmpty() ? "no takes yet" : label_,
                    getLocalBounds().reduced (6, 0),
                    juce::Justification::centredLeft, true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging_ || e.getDistanceFromDragStart() < 6)
            return;

        const auto file = newest_;

        if (! file.existsAsFile())
        {
            say ("float_probe_drag_no_file");
            return;
        }

        dragging_ = true;

        // The real question behind the real question: does a drag STARTED from
        // a desktop component still reach the host? getNSViewForDragEvent takes
        // sourceComp->getWindowHandle(), which for this component is its own
        // NSView rather than the editor's -- a different view, in a different
        // window, from the one the DAW knows about.
        const auto ok = juce::DragAndDropContainer::performExternalDragDropOfFiles (
            { file.getFullPathName() }, false, this, [this] { dragging_ = false; });

        say ("float_probe_drag started=" + juce::String (ok ? 1 : 0)
             + " file=" + file.getFileName().quoted());

        if (! ok)
            dragging_ = false;
    }

    void mouseUp (const juce::MouseEvent&) override { dragging_ = false; }

private:
    static constexpr int kWidth  = 210;
    static constexpr int kHeight = 30;
    static constexpr int kInset  = 8;

    void say (const juce::String& line) const
    {
        if (log_ != nullptr)
            log_ (line);
    }

    /** Puts the handle on the anchor's bottom-right corner, in screen space.

        Called from the watcher rather than a timer, so a window drag moves it
        in the same pass that moves the editor instead of up to one tick later.
    */
    void follow()
    {
        if (! isVisible())
            return;

        const auto anchorBounds = anchor_.getScreenBounds();

        // A zero-size anchor happens during teardown and while the editor is
        // being reparented. Chasing it would park the handle at the top-left of
        // the screen for a frame.
        if (anchorBounds.isEmpty())
            return;

        setBounds (anchorBounds.getRight()  - kWidth  - kInset,
                   anchorBounds.getBottom() - kHeight - kInset,
                   kWidth, kHeight);
    }

    // ── ComponentMovementWatcher ─────────────────────────────────────────────

    void componentMovedOrResized (bool, bool) override { follow(); }

    /** The editor being reparented into a different host window, which is the
        case the original timer was hedging against. Answered directly here.
    */
    void componentPeerChanged() override
    {
        say ("float_probe_peer_changed");
        follow();
    }

    void componentVisibilityChanged() override { updateVisibility(); }

    // ── Timer: only what no callback reports ─────────────────────────────────

    void updateVisibility()
    {
        // ABUSE 3, attempted. An always-on-top window belongs to this process,
        // not to this plugin, so it will happily sit over Finder or a browser
        // unless something takes it away. isForegroundProcess is the coarse
        // half; the editor actually showing is the other.
        const bool shouldShow = anchor_.isShowing()
                             && juce::Process::isForegroundProcess();

        if (shouldShow == wasShowing_)
            return;

        wasShowing_ = shouldShow;
        setVisible (shouldShow);

        // Reposition on the way back in: the window may well have moved while
        // the handle was hidden and taking no callbacks.
        if (shouldShow)
            follow();

        say (juce::String ("float_probe_visible=") + (shouldShow ? "1" : "0"));
    }

    void timerCallback() override
    {
        updateVisibility();

        if (! wasShowing_)
            return;

        // Cheap: list(1) is one directory read, and the label only matters for
        // recognising which file a successful drop delivered.
        const auto rows = kromosynth::renders::list (1);
        const auto next = rows.empty() ? juce::File() : rows.front().file;

        if (next != newest_)
        {
            newest_ = next;
            label_ = rows.empty() ? juce::String() : rows.front().name;
            repaint();
        }
    }

    juce::Component& anchor_;
    std::function<void (const juce::String&)> log_;

    juce::File newest_;
    juce::String label_;
    bool dragging_ = false;
    bool wasShowing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingDragProbe)
};

#endif // KROMOSYNTH_FLOAT_PROBE && ! JUCE_IOS
