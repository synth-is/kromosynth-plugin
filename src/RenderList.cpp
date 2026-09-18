#include "RenderList.h"

#if ! JUCE_IOS

#include "Palette.h"

namespace
{

/** The outbox's state as one comparable number.

    Björn's, moved here with RenderGutter's removal. THE NEWEST WAV, NOT THE
    DIRECTORY'S MTIME: a directory's mtime changes when any child changes,
    including Ableton writing its own `.asd` analysis sidecar next to a file we
    just handed it, which made every successful drag trigger a rescan two
    seconds later -- the list reacting to Live reacting to the list.

    Counting the wavs as well as timing them, so a deletion is noticed too:
    removing a file can leave an older newest-mtime than before, which a
    timestamp alone reads as "nothing happened".
*/
juce::int64 outboxStamp (const juce::File& dir)
{
    if (! dir.isDirectory())
        return 0;

    juce::Array<juce::File> files;
    dir.findChildFiles (files, juce::File::findFiles, false, "*.wav");

    juce::int64 newest = 0;
    for (const auto& f : files)
        newest = juce::jmax (newest, f.getLastModificationTime().toMilliseconds());

    return newest * 1000 + (juce::int64) files.size();
}

juce::String fmtSeconds (double s)
{
    if (s <= 0.0)
        return {};

    return (s < 10.0 ? juce::String (s, 1) : juce::String (juce::roundToInt (s))) + "s";
}

juce::String fmtBytes (juce::int64 b)
{
    if (b <= 0)
        return {};

    return b >= 1048576 ? juce::String ((double) b / 1048576.0, 1) + " MB"
                        : juce::String (juce::jmax (1, (int) (b / 1024))) + " kB";
}

} // namespace

RenderList::RenderList()
{
    refresh();

    // Two seconds, and it does one stat-and-count rather than a full read. The
    // outbox changes when this plugin puts something there, which the page also
    // reports -- but also when Finder deletes a file or, on iOS, the container
    // app drains it, and nothing reports those.
    startTimer (2000);
}

RenderList::~RenderList() { stopTimer(); }

void RenderList::say (const juce::String& line) const
{
    if (onLog != nullptr)
        onLog (line);
}

void RenderList::refresh()
{
    rows_.clear();

    for (const auto& e : kromosynth::renders::list())
        rows_.push_back ({ e, {}, {}, {}, {} });

    stamp_ = outboxStamp (kromosynth::renders::outbox());

    // A file can vanish between reads, so a Play button must not stay lit for
    // something that is gone.
    if (playingFile_.isNotEmpty())
    {
        const auto still = std::any_of (rows_.begin(), rows_.end(),
            [this] (const Row& r) { return r.entry.file.getFileName() == playingFile_; });

        if (! still)
            playingFile_.clear();
    }

    hoveredRow_ = -1;

    layOutRows();
    clampScroll();
    repaint();
}

void RenderList::setCompact (bool shouldBeCompact)
{
    if (compact_ == shouldBeCompact)
        return;

    compact_ = shouldBeCompact;
    scrollY_ = 0;      // a one-row view that remembered a scroll would show nothing

    layOutRows();
    repaint();
}

void RenderList::setPlayingFile (const juce::String& fileName)
{
    if (playingFile_ == fileName)
        return;

    playingFile_ = fileName;
    repaint();
}

void RenderList::layOutRows()
{
    const int w = juce::jmax (0, getWidth() - kPad * 2);
    int y = kPad;

    // Compact is ONE row, the newest, filling the band. Laying the rest out
    // below would put them past the bottom edge where paint() skips them but
    // rowAt() would still match -- an invisible row that answers to a drag.
    if (compact_)
    {
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            auto& r = rows_[i];

            if (i > 0) { r.bounds = {}; r.play = {}; r.del = {}; continue; }

            const int h = juce::jmax (kRowH, getHeight() - kPad);
            r.bounds = { kPad, kPad / 2, w, h - kPad / 2 };

            const int cy = r.bounds.getCentreY() - kBtnH / 2;

            // Dismiss sits outermost, where a close box belongs, and only in
            // compact mode -- the Sounds tab cannot be dismissed, it is a tab.
            r.dismiss = { kPad + w - kPad - kBtnH,           cy, kBtnH, kBtnH };
            r.del     = { r.dismiss.getX() - kRowGap - kBtnW, cy, kBtnW, kBtnH };
            r.play    = { r.del.getX() - kRowGap - kBtnW,     cy, kBtnW, kBtnH };
        }

        return;
    }

    for (auto& r : rows_)
    {
        r.bounds = { kPad, y, w, kRowH };

        const int cy = y + (kRowH - kBtnH) / 2;

        r.dismiss = {};   // list mode has no dismiss; see the compact branch
        r.del  = { kPad + w - kPad - kBtnW,                cy, kBtnW, kBtnH };
        r.play = { r.del.getX() - kRowGap - kBtnW,         cy, kBtnW, kBtnH };

        y += kRowH + kRowGap;
    }
}

void RenderList::clampScroll()
{
    const int contentH = rows_.empty() ? 0
                       : (int) rows_.size() * (kRowH + kRowGap) + kPad * 2;

    scrollY_ = juce::jlimit (0, juce::jmax (0, contentH - getHeight()), scrollY_);
}

void RenderList::resized()
{
    layOutRows();
    clampScroll();
}

int RenderList::rowAt (juce::Point<int> p) const
{
    const auto inList = p.translated (0, scrollY_);

    for (int i = 0; i < (int) rows_.size(); ++i)
        if (! rows_[(size_t) i].bounds.isEmpty()
            && rows_[(size_t) i].bounds.contains (inList))
            return i;

    return -1;
}

void RenderList::paint (juce::Graphics& g)
{
    using namespace kromosynth::palette;

    g.fillAll (toColour (surfaceBase));

    if (rows_.empty())
    {
        // The editor declines to show this at all when compact and empty, so
        // this paragraph is only ever the Sounds tab's empty state.
        if (compact_)
            return;

        g.setColour (toColour (textMuted));
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Sounds you take land here, ready to drag into your DAW.\n\n"
                          "Render a one-shot on the Feed and it will arrive by itself.",
                          getLocalBounds().reduced (24), juce::Justification::centred, 4);
        return;
    }

    for (int i = 0; i < (int) rows_.size(); ++i)
    {
        const auto& r = rows_[(size_t) i];

        if (r.bounds.isEmpty())
            continue;   // compact mode zeroes every row but the newest

        const auto box = r.bounds.translated (0, -scrollY_);

        if (! box.intersects (getLocalBounds()))
            continue;   // scrolled out; drawing it would be wasted

        const bool hot = (i == hoveredRow_);

        g.setColour (toColour (hot ? surfaceRaised : surfacePanel));
        g.fillRoundedRectangle (box.toFloat(), 7.0f);

        g.setColour (toColour (borderSubtle));
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 7.0f, 1.0f);

        const auto playBox = r.play.translated (0, -scrollY_);
        const auto delBox  = r.del.translated (0, -scrollY_);

        // Name gets whatever the buttons and the meta do not, and is elided
        // rather than allowed to run under them.
        const auto meta = [&]
        {
            juce::StringArray parts;
            parts.addIfNotAlreadyThere (fmtSeconds (r.entry.seconds));
            parts.addIfNotAlreadyThere (fmtBytes (r.entry.bytes));
            parts.removeEmptyStrings();
            return parts.joinIntoString (" \xc2\xb7 ");
        }();

        const int metaW = meta.isEmpty() ? 0 : 108;

        // A GRIP, because nothing else says these rows are draggable. Björn:
        // "would be nice to have some visual indicator that the Sounds rows can
        // be dragged". Two columns of dots is the convention for a drag handle
        // and costs no vertical space; the cursor change in mouseMove confirms
        // it on hover. The whole row is the drag source, not just the grip --
        // this only advertises.
        {
            g.setColour (toColour (hot ? textBody : textMuted));

            const int gx = box.getX() + 10;
            const int gy = box.getCentreY() - 5;

            for (int dx = 0; dx < 2; ++dx)
                for (int dy = 0; dy < 3; ++dy)
                    g.fillRect (gx + dx * 4, gy + dy * 5, 2, 2);
        }

        g.setColour (toColour (textPrimary));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (r.entry.name,
                    box.withTrimmedLeft (24)
                       .withTrimmedRight (box.getRight() - playBox.getX() + metaW + 10),
                    juce::Justification::centredLeft, true);

        if (metaW > 0)
        {
            g.setColour (toColour (textMuted));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (meta,
                        juce::Rectangle<int> (playBox.getX() - metaW - 10, box.getY(),
                                              metaW, box.getHeight()),
                        juce::Justification::centredRight, false);
        }

        const bool playing = r.entry.file.getFileName() == playingFile_;

        auto drawButton = [&g] (juce::Rectangle<int> b, const juce::String& text,
                                juce::Colour edge, juce::Colour ink)
        {
            g.setColour (edge);
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 5.0f, 1.0f);
            g.setColour (ink);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (text, b, juce::Justification::centred, false);
        };

        drawButton (playBox, playing ? "Stop" : "Play",
                    toColour (playing ? accent : border),
                    toColour (playing ? accent : textBody));

        drawButton (delBox, "Delete", toColour (border), toColour (textBody));

        if (! r.dismiss.isEmpty())
            drawButton (r.dismiss.translated (0, -scrollY_), juce::String::charToString (0x00d7),
                        toColour (border), toColour (textMuted));
    }
}

void RenderList::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = rowAt (e.getPosition());

    if (hit != hoveredRow_)
    {
        hoveredRow_ = hit;
        repaint();
    }

    // The cursor is the other half of the affordance: an open hand over the
    // row body says "this can be picked up", and the normal pointer over Play
    // and Delete says those are buttons rather than a way to start a drag --
    // which matters because mouseDrag refuses to begin one from them.
    bool draggable = false;

    if (hit >= 0)
    {
        const auto& r = rows_[(size_t) hit];
        const auto p = e.getPosition().translated (0, scrollY_);

        draggable = ! r.play.contains (p) && ! r.del.contains (p)
                 && ! (! r.dismiss.isEmpty() && r.dismiss.contains (p));
    }

    setMouseCursor (draggable ? juce::MouseCursor::DraggingHandCursor
                              : juce::MouseCursor::NormalCursor);
}

void RenderList::mouseExit (const juce::MouseEvent&)
{
    if (hoveredRow_ != -1)
    {
        hoveredRow_ = -1;
        repaint();
    }
}

void RenderList::mouseDown (const juce::MouseEvent& e)
{
    const auto hit = rowAt (e.getPosition());

    if (hit < 0)
        return;

    const auto& r = rows_[(size_t) hit];
    const auto p = e.getPosition().translated (0, scrollY_);

    if (r.play.contains (p))
    {
        const auto name = r.entry.file.getFileName();

        // Toggling the row that is sounding stops it, which is what a second
        // press of a play button means to everyone. The page owns the audio, so
        // it is told and reports back through setPlayingFile.
        if (onPlayRequest != nullptr)
            onPlayRequest (playingFile_ == name ? juce::String() : name);

        return;
    }

    if (r.del.contains (p))
    {
        deleteRow (hit);
        return;
    }

    if (! r.dismiss.isEmpty() && r.dismiss.contains (p))
    {
        dismissed_ = true;

        if (playingFile_.isNotEmpty() && onPlayRequest != nullptr)
            onPlayRequest ({});

        say ("render_handle_dismissed");

        if (onHandleWantedChanged != nullptr)
            onHandleWantedChanged();

        return;
    }
}

void RenderList::deleteRow (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) rows_.size()))
        return;

    const auto file = rows_[(size_t) index].entry.file;

    // ASYNC, and the synchronous form was a silent no-op. The int-returning
    // showYesNoBox only blocks and returns a real answer under
    // JUCE_MODAL_LOOPS_PERMITTED, which is OFF in a plugin -- so with a null
    // callback it showed nothing and returned 0, `if (ok != 1) return;` bailed
    // every time, and Delete never deleted anything. Björn: "if i press delete,
    // that bar doesn't change." It did nothing at all, quietly, and it compiled
    // because both overloads accept these five arguments.
    //
    // It ASKS FIRST because this removes a file the user spent extraction quota
    // on. The message says the sound stays theirs, which is true under
    // per-sound pricing and is what makes offering a delete reasonable.
    juce::NativeMessageBox::showYesNoBox (
        juce::MessageBoxIconType::WarningIcon,
        "Delete this sound?",
        file.getFileNameWithoutExtension()
            + "\n\nThe sound stays yours on synth.is and can be taken again for free.",
        this,
        // SafePointer, because the callback outlives the click: the editor can
        // be closed while the box is up, and this component goes with it.
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<RenderList> (this), file] (int result)
            {
                if (result != 1 || safe == nullptr)
                    return;

                if (safe->playingFile_ == file.getFileName() && safe->onPlayRequest != nullptr)
                    safe->onPlayRequest ({});

                // Ableton leaves an .asd analysis sidecar beside any wav it has
                // read. Deleting the wav and leaving that behind accumulates
                // orphans in a folder the user is being encouraged to browse.
                const auto sidecar = file.getSiblingFile (file.getFileName() + ".asd");

                if (sidecar.existsAsFile())
                    sidecar.deleteFile();

                const bool gone = file.deleteFile();

                safe->say ("render_deleted ok=" + juce::String (gone ? 1 : 0)
                           + " " + file.getFileName().quoted());

                safe->refresh();

                if (safe->onContentsChanged != nullptr)
                    safe->onContentsChanged();

                // Deleting the last take takes the band away with it.
                if (safe->onHandleWantedChanged != nullptr)
                    safe->onHandleWantedChanged();
            }));
}

void RenderList::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging_ || e.getDistanceFromDragStart() < kDragSlop)
        return;

    // The row the gesture STARTED on, so a drag begun on one row cannot
    // deliver another after the pointer has moved.
    const auto hit = rowAt (e.getMouseDownPosition());

    if (hit < 0)
        return;

    const auto& r = rows_[(size_t) hit];
    const auto down = e.getMouseDownPosition().translated (0, scrollY_);

    // Pressing a button is not the start of a drag. Without this, a slightly
    // shaky click on Delete would begin dragging the file instead.
    if (r.play.contains (down) || r.del.contains (down)
        || (! r.dismiss.isEmpty() && r.dismiss.contains (down)))
        return;

    if (! r.entry.file.existsAsFile())
    {
        say ("render_drag_stale " + r.entry.file.getFileName());
        refresh();
        return;
    }

    dragging_ = true;

    // canMoveFiles FALSE. True lets the receiving host MOVE the file, which
    // would delete the sound out of the folder this list exists to show.
    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { r.entry.file.getFullPathName() }, false, this, [this] { dragging_ = false; });

    say ("render_drag_started=" + juce::String (started ? 1 : 0)
         + " file=" + r.entry.file.getFileName().quoted());

    // A session that never began will never call the callback, so clearing the
    // flag here is what stops one failure making the list permanently
    // undraggable.
    if (! started)
        dragging_ = false;
}

void RenderList::mouseUp (const juce::MouseEvent&) { dragging_ = false; }

void RenderList::mouseWheelMove (const juce::MouseEvent&,
                                 const juce::MouseWheelDetails& wheel)
{
    if (compact_)
        return;

    const int before = scrollY_;

    scrollY_ -= juce::roundToInt (wheel.deltaY * (float) (kRowH + kRowGap) * 3.0f);
    clampScroll();

    if (scrollY_ != before)
        repaint();
}

void RenderList::timerCallback()
{
    const auto stamp = outboxStamp (kromosynth::renders::outbox());

    if (stamp == stamp_)
        return;

    const auto before = rows_.size();

    // A NEW TAKE BRINGS THE BAND BACK. Dismissal means "not this one", not
    // "never again" -- the next sound you take is exactly the case the handle
    // exists for.
    const bool wasWanted = wantsHandle();

    dismissed_ = false;

    refresh();

    say ("renders_rescanned was=" + juce::String ((int) before)
         + " now=" + juce::String ((int) rows_.size()));

    // The page's Sounds badge is now wrong, and only it can fix that.
    if (onContentsChanged != nullptr)
        onContentsChanged();

    if (wasWanted != wantsHandle() && onHandleWantedChanged != nullptr)
        onHandleWantedChanged();
}

#endif // ! JUCE_IOS
