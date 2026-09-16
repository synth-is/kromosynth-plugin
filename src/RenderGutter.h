// RenderGutter — the native strip that a render can be dragged out of.
//
// DESKTOP ONLY, and not by preference. juce_Windowing_ios.mm:527 is the whole
// argument:
//
//     bool DragAndDropContainer::performExternalDragDropOfFiles (...)
//     {
//         jassertfalse;    // no such thing on iOS!
//         return false;
//     }
//
// ── Why this is native rather than a row in the shell page ──────────────────
//
// It was going to be HTML. A "Renders" section beside Instruments, styled with
// the same palette, draggable rows -- which is where a user would look for it
// and is what was asked for. Measured 2026-09-15 and it does not work:
//
// WebKit writes a page-initiated drag of a file URL to the pasteboard as
// `public.url` -- a LINK -- and never as `public.file-url`, a FILE. Dropping
// row 1 of tools/drag-spike on a Finder window produced `Users:.fileloc`, a
// bookmark document pointing at the right path, rather than a copy of the WAV.
// A plain <a href="file://..."> would not begin a drag at all. Ableton took
// nothing. The URL itself arrived intact, so this is policy and not mangling:
// WebKit will not hand another application a usable local-file reference from
// a remote origin, which is the boundary anyone would want it to hold.
//
// So the drag has to be started by a native component during a real mouseDrag,
// which is the only moment `[[view window] currentEvent]` is a live NSEvent --
// the thing juce_Windowing_mac.mm:192 needs and the async
// window.__JUCE__ bridge can never provide.
//
// ── And why it is a separate rectangle rather than an overlay ────────────────
//
// juce_NSViewComponent.h, on the webview this sits beside:
//
//     since the view is a native object, it'll obliterate any JUCE components
//     that may overlap this component, but that's life
//
// A drag chip floated over the browser would be invisible. The editor trims
// kHeight off its bounds BEFORE placing either webview, so this strip owns a
// rectangle no native view occupies.
//
// ── What this slice does not do, stated so it is not assumed ─────────────────
//
// No preview: clicking selects, nothing sounds. Auditioning needs an audio
// path and a transport and has its own questions about the voice budget.
// No delete: Finder owns that. No scrolling: as many chips as fit, newest
// first, with Reveal covering the rest -- newest-first is the ordering that
// makes that acceptable rather than a gap.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if ! JUCE_IOS

#include <functional>
#include <vector>

class RenderGutter final : public juce::Component,
                           private juce::Timer
{
public:
    RenderGutter();
    ~RenderGutter() override;

    /** Logical pixels. The editor reserves exactly this and no more. */
    static constexpr int kHeight = 58;

    /** Log sink, injected so this class needs no reference to the processor.
        Every drag and every rescan reports, because "I dragged it and nothing
        happened" has three possible causes and only the log separates them.
    */
    std::function<void (const juce::String&)> onLog;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Entry
    {
        juce::File file;
        juce::String label;
        juce::Rectangle<int> chip;
    };

    void timerCallback() override;

    /** Reads the outbox. Cheap: one directory listing, capped. */
    void rescan();

    int indexAt (juce::Point<int> p) const;
    void say (const juce::String& line) const;

    std::vector<Entry> entries_;
    int selected_ = -1;

    /** The outbox's own modification time, so the timer can do one stat and
        stop. Polled rather than pushed from the acquisition completion
        handler, deliberately: this OBSERVES the directory, so it notices a
        file arriving from Finder, from a second plugin instance, or from a
        path nobody has thought of yet. A broadcaster would only ever report
        what the plugin believed it had done, and this project's findings file
        is largely a list of layers that asserted exactly that.
    */
    juce::int64 folderStamp_ = 0;

    /** One drag per gesture. performExternalDragDropOfFiles is asynchronous
        and mouseDrag fires repeatedly, so without this a single gesture starts
        a session per pixel moved.
    */
    bool dragging_ = false;

    juce::TextButton reveal_ { "Reveal" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderGutter)
};

#endif // ! JUCE_IOS
