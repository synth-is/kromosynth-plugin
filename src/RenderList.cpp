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
        rows_.push_back ({ e, {}, {}, {} });

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

    for (auto& r : rows_)
    {
        r.bounds = { kPad, y, w, kRowH };

        const int cy = y + (kRowH - kBtnH) / 2;

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
        if (rows_[(size_t) i].bounds.contains (inList))
            return i;

    return -1;
}

void RenderList::paint (juce::Graphics& g)
{
    using namespace kromosynth::palette;

    g.fillAll (toColour (surfaceBase));

    if (rows_.empty())
    {
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

        g.setColour (toColour (textPrimary));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (r.entry.name,
                    box.withTrimmedLeft (10)
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
}

void RenderList::deleteRow (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) rows_.size()))
        return;

    const auto file = rows_[(size_t) index].entry.file;

    // ASKS FIRST, because this removes a file the user spent extraction quota
    // on. The message says the sound stays theirs, which is true under
    // per-sound pricing and is what makes offering a delete reasonable at all.
    const auto ok = juce::NativeMessageBox::showYesNoBox (
        juce::MessageBoxIconType::WarningIcon,
        "Delete this sound?",
        file.getFileNameWithoutExtension()
            + "\n\nThe sound stays yours on synth.is and can be taken again for free.",
        this, nullptr);

    if (ok != 1)
        return;

    if (playingFile_ == file.getFileName() && onPlayRequest != nullptr)
        onPlayRequest ({});

    // Ableton leaves an .asd analysis sidecar beside any wav it has read.
    // Deleting the wav and leaving that behind accumulates orphans in a folder
    // the user is being encouraged to browse.
    const auto sidecar = file.getSiblingFile (file.getFileName() + ".asd");

    if (sidecar.existsAsFile())
        sidecar.deleteFile();

    const bool gone = file.deleteFile();

    say ("render_deleted ok=" + juce::String (gone ? 1 : 0)
         + " " + file.getFileName().quoted());

    refresh();

    if (onContentsChanged != nullptr)
        onContentsChanged();
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
    if (r.play.contains (down) || r.del.contains (down))
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

    refresh();

    say ("renders_rescanned was=" + juce::String ((int) before)
         + " now=" + juce::String ((int) rows_.size()));

    // The page's Sounds badge is now wrong, and only it can fix that.
    if (onContentsChanged != nullptr)
        onContentsChanged();
}

#endif // ! JUCE_IOS
