#include "RenderGutter.h"

#if ! JUCE_IOS

#include "Palette.h"
#include "RenderOutbox.h"

namespace
{

constexpr int kPad        = 10;
constexpr int kChipW      = 168;
constexpr int kChipH      = 34;
constexpr int kChipGap    = 6;
constexpr int kRevealW    = 74;
constexpr int kDragSlop   = 6;    // px before a click becomes a drag
constexpr int kMaxEntries = 24;

/** Newest first, for juce::Array::sort. */
struct ByNewestFirst
{
    static int compareElements (const juce::File& a, const juce::File& b)
    {
        const auto ta = a.getLastModificationTime().toMilliseconds();
        const auto tb = b.getLastModificationTime().toMilliseconds();

        return ta == tb ? 0 : (ta > tb ? -1 : 1);
    }
};

} // namespace

RenderGutter::RenderGutter()
{
    addAndMakeVisible (reveal_);
    reveal_.setButtonText ("Reveal");
    reveal_.onClick = [this]
    {
        auto dir = kromosynth::renders::outbox();

        // createDirectory rather than a bare reveal: the folder does not exist
        // until the first render lands, and revealing nothing looks like a
        // broken button rather than an empty shelf.
        dir.createDirectory();
        dir.revealToUser();

        say ("renders_reveal " + dir.getFullPathName());
    };

    rescan();

    // Two seconds. Slow enough to be free -- one stat -- and fast enough that
    // a render finishing while the editor is open appears without the user
    // wondering whether it worked.
    startTimer (2000);
}

RenderGutter::~RenderGutter() { stopTimer(); }

void RenderGutter::say (const juce::String& line) const
{
    if (onLog != nullptr)
        onLog (line);
}

void RenderGutter::rescan()
{
    entries_.clear();

    const auto dir = kromosynth::renders::outbox();

    if (dir.isDirectory())
    {
        juce::Array<juce::File> files;
        dir.findChildFiles (files, juce::File::findFiles, false, "*.wav");

        // Newest first. Not alphabetical: the thing you just rendered is the
        // thing you want, and it is the only ordering that makes "as many as
        // fit" an acceptable substitute for scrolling.
        ByNewestFirst comparator;
        files.sort (comparator, true);

        for (const auto& f : files)
        {
            if ((int) entries_.size() >= kMaxEntries)
                break;

            entries_.push_back ({ f, f.getFileNameWithoutExtension(), {} });
        }
    }

    folderStamp_ = dir.isDirectory() ? dir.getLastModificationTime().toMilliseconds() : 0;

    if (selected_ >= (int) entries_.size())
        selected_ = entries_.empty() ? -1 : 0;
    else if (selected_ < 0 && ! entries_.empty())
        selected_ = 0;

    resized();
    repaint();
}

void RenderGutter::timerCallback()
{
    const auto dir = kromosynth::renders::outbox();
    const auto stamp = dir.isDirectory()
                     ? dir.getLastModificationTime().toMilliseconds()
                     : 0;

    if (stamp == folderStamp_)
        return;

    const auto before = entries_.size();
    rescan();

    say ("renders_rescanned was=" + juce::String ((int) before)
         + " now=" + juce::String ((int) entries_.size()));
}

void RenderGutter::resized()
{
    auto area = getLocalBounds().reduced (kPad, 0);

    reveal_.setBounds (area.removeFromRight (kRevealW)
                           .withSizeKeepingCentre (kRevealW, kChipH - 4));

    area.removeFromRight (kChipGap);

    int x = area.getX();

    for (auto& e : entries_)
    {
        e.chip = { x, area.getCentreY() - kChipH / 2, kChipW, kChipH };
        x += kChipW + kChipGap;

        // Off the right edge: left with a zero-width chip so indexAt can never
        // match it. A chip the user cannot see must not be draggable either.
        if (e.chip.getRight() > area.getRight())
            e.chip = {};
    }
}

int RenderGutter::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) entries_.size(); ++i)
        if (! entries_[(size_t) i].chip.isEmpty() && entries_[(size_t) i].chip.contains (p))
            return i;

    return -1;
}

void RenderGutter::paint (juce::Graphics& g)
{
    using namespace kromosynth::palette;

    g.fillAll (toColour (surfaceDeep));

    g.setColour (toColour (borderSubtle));
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    if (entries_.empty())
    {
        g.setColour (toColour (textMuted));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Sounds you take land here, ready to drag into your DAW.",
                    getLocalBounds().reduced (kPad, 0),
                    juce::Justification::centredLeft, true);
        return;
    }

    for (int i = 0; i < (int) entries_.size(); ++i)
    {
        const auto& e = entries_[(size_t) i];

        if (e.chip.isEmpty())
            continue;

        const auto chip = e.chip.toFloat();
        const bool isSel = (i == selected_);

        g.setColour (toColour (isSel ? accentSurface : surfacePanel));
        g.fillRoundedRectangle (chip, 6.0f);

        g.setColour (isSel ? toColour (accent) : toColour (border));
        g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (toColour (isSel ? textPrimary : textBody));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (e.label, e.chip.reduced (9, 0),
                    juce::Justification::centredLeft, true);
    }
}

void RenderGutter::mouseDown (const juce::MouseEvent& e)
{
    const auto hit = indexAt (e.getPosition());

    if (hit >= 0 && hit != selected_)
    {
        selected_ = hit;
        repaint();
    }
}

void RenderGutter::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging_ || selected_ < 0)
        return;

    if (e.getDistanceFromDragStart() < kDragSlop)
        return;

    // The chip under the mouse when the gesture STARTED, not the selection,
    // so a drag begun on one chip cannot deliver another.
    const auto hit = indexAt (e.getMouseDownPosition());

    if (hit < 0)
        return;

    const auto& file = entries_[(size_t) hit].file;

    if (! file.existsAsFile())
    {
        // Deleted under us between the last rescan and this gesture. Rescan
        // rather than drag a path that is no longer there.
        say ("render_drag_stale " + file.getFileName());
        rescan();
        return;
    }

    dragging_ = true;

    // canMoveFiles FALSE. True lets the receiving host MOVE the file, which
    // would delete the render out of the folder the user was just told to look
    // in -- and out of the Files.app placement on iOS's side of the same
    // design.
    const auto started = juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { file.getFullPathName() },
        /*canMoveFiles*/ false,
        this,
        [this] { dragging_ = false; });

    say ("render_drag_started=" + juce::String (started ? 1 : 0)
         + " file=" + file.getFileName().quoted());

    // A session that never began will never call the callback, so the flag has
    // to be cleared here or the gutter goes permanently undraggable after one
    // failure.
    if (! started)
        dragging_ = false;
}

void RenderGutter::mouseUp (const juce::MouseEvent&)
{
    // Belt and braces. macOS synthesises a mouseUp when a dragging session
    // ends (juce_Windowing_mac.mm does it explicitly, because the source view
    // does not get one), so this usually runs after the callback already
    // cleared the flag. It costs nothing and it means a platform that skips
    // the callback does not strand the strip.
    dragging_ = false;
}

#endif // ! JUCE_IOS
