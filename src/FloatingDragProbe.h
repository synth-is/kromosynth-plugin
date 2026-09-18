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
// Tracked on a TIMER rather than from the editor's moved()/resized(). A plugin
// editor is reparented into a host window it does not own and does not reliably
// hear about being dragged around the screen. A probe that used the callbacks
// would be measuring JUCE's notification coverage instead of the thing in
// question.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if KROMOSYNTH_FLOAT_PROBE && ! JUCE_IOS

#include "RenderOutbox.h"

class FloatingDragProbe final : public juce::Component,
                                private juce::Timer
{
public:
    /** @param anchor  the editor to track. Held by reference: this probe is
                       owned by that editor and cannot outlive it.
        @param log     where to report, so the probe needs no processor.
    */
    FloatingDragProbe (juce::Component& anchor, std::function<void (const juce::String&)> log)
        : anchor_ (anchor), log_ (std::move (log))
    {
        setSize (kWidth, kHeight);

        // windowIsTemporary: a utility window, no titlebar, not in the window
        // menu. Nothing about this should look like a document.
        setAlwaysOnTop (true);
        addToDesktop (juce::ComponentPeer::windowIsTemporary);

        startTimerHz (20);

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

    void timerCallback() override
    {
        // ABUSE 3, attempted. An always-on-top window belongs to this process,
        // not to this plugin, so it will happily sit over Finder or a browser
        // unless something takes it away. isForegroundProcess is the coarse
        // half; the editor actually showing is the other.
        const bool shouldShow = anchor_.isShowing()
                             && juce::Process::isForegroundProcess();

        if (shouldShow != wasShowing_)
        {
            wasShowing_ = shouldShow;
            setVisible (shouldShow);
            say (juce::String ("float_probe_visible=") + (shouldShow ? "1" : "0"));
        }

        if (! shouldShow)
            return;

        // ABUSES 1 AND 2. Screen coordinates, read fresh every tick, because
        // the editor is reparented into a window this process does not own.
        const auto anchorBounds = anchor_.getScreenBounds();

        const juce::Rectangle<int> target (
            anchorBounds.getRight()  - kWidth  - kInset,
            anchorBounds.getBottom() - kHeight - kInset,
            kWidth, kHeight);

        if (target != getBounds())
        {
            setBounds (target);

            // Throttled: at 20 Hz a moving window would fill the log with
            // thousands of lines and bury everything else in it.
            if (++moveTicks_ % 20 == 1)
                say ("float_probe_moved to=" + target.toString()
                     + " anchor=" + anchorBounds.toString());
        }

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
    int moveTicks_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingDragProbe)
};

#endif // KROMOSYNTH_FLOAT_PROBE && ! JUCE_IOS
