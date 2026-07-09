// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows tracks as lanes and clips as blocks, edited through the command stack

#include "ui/ArrangeView.h"

#include "model/Commands.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace howl::ui {

// Stores references to the arrangement, transport, and command stack, starts the playhead timer
ArrangeView::ArrangeView(model::Arrangement& arrangement, engine::Transport& transport,
                          model::CommandStack& commandStack, double sampleRate,
                          std::function<model::SnapDivision()> snapProvider)
    : m_arrangement(arrangement)
    , m_transport(transport)
    , m_commandStack(commandStack)
    , m_sampleRate(sampleRate)
    , m_snapProvider(std::move(snapProvider))
{
    // Without this, grabKeyboardFocus and click-to-focus are no-ops and keyPressed never fires
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

// Stops the playhead timer
ArrangeView::~ArrangeView() {
    stopTimer();
}

// Repaints so the playhead position stays current during playback
void ArrangeView::timerCallback() {
    repaint();
}

// Returns the current global snap division, Step when no provider is set
model::SnapDivision ArrangeView::snapDivision() const {
    return m_snapProvider ? m_snapProvider() : model::SnapDivision::Step;
}

// Samples per tick from the transport's current tempo and this view's sample rate
double ArrangeView::samplesPerTick() const {
    const double tempo = m_transport.tempo();
    return (60.0 / tempo) * m_sampleRate / static_cast<double>(model::kTicksPerQuarter);
}

// Ticks spanned by the visible timeline, covers every placed clip plus a sensible minimum
int64_t ArrangeView::visibleTickSpan() const {
    int64_t maxEndTick = kMinimumVisibleTicks;
    const double spt = samplesPerTick();

    for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
        const model::Track& track = m_arrangement.track(i);

        for (const auto& placement : track.midiClips) {
            const int64_t endTick = placement.startTick + placement.clip.lengthTicks();
            maxEndTick = endTick > maxEndTick ? endTick : maxEndTick;
        }

        for (const auto& placement : track.audioClips) {
            const auto lengthTicks = static_cast<int64_t>(static_cast<double>(placement.clip.activeLengthSamples()) / spt);
            const int64_t endTick = placement.startTick + lengthTicks;
            maxEndTick = endTick > maxEndTick ? endTick : maxEndTick;
        }
    }

    return maxEndTick + model::kTicksPerQuarter * 4;
}

// Ticks spanned by the current zoom level, visibleTickSpan() / m_zoom
double ArrangeView::zoomedVisibleSpan() const {
    return static_cast<double>(visibleTickSpan()) / m_zoom;
}

// Clamps m_scrollTick to [0, visibleTickSpan() - zoomedVisibleSpan()]
void ArrangeView::clampScroll() {
    const double maxScroll = juce::jmax(0.0, static_cast<double>(visibleTickSpan()) - zoomedVisibleSpan());
    m_scrollTick = static_cast<int64_t>(juce::jlimit(0.0, maxScroll, static_cast<double>(m_scrollTick)));
}

// Converts a pixel x position to a tick, offset by the current scroll
int64_t ArrangeView::xToTick(int x) const {
    const double span = zoomedVisibleSpan();
    const double ratio = juce::jlimit(0.0, 1.0, static_cast<double>(x) / static_cast<double>(getWidth()));
    return m_scrollTick + static_cast<int64_t>(ratio * span);
}

// Converts a tick to a pixel x position, offset by the current scroll
float ArrangeView::tickToX(int64_t tick) const {
    const double span = zoomedVisibleSpan();
    return static_cast<float>(static_cast<double>(tick - m_scrollTick) / span * static_cast<double>(getWidth()));
}

// Returns the height of one track lane, below the ruler
float ArrangeView::laneHeight() const {
    const std::size_t numTracks = m_arrangement.numTracks();
    const float available = static_cast<float>(getHeight() - kRulerHeight);
    if (numTracks == 0) {
        return available;
    }
    return available / static_cast<float>(numTracks);
}

// Converts a pixel y position to a track index, clamped to numTracks() - 1
std::size_t ArrangeView::yToTrackIndex(int y) const {
    const std::size_t numTracks = m_arrangement.numTracks();
    if (numTracks == 0) {
        return 0;
    }

    const float height = laneHeight();
    const int index = static_cast<int>(static_cast<float>(y - kRulerHeight) / height);
    return static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(numTracks) - 1, index));
}

// Returns the current transport position converted to ticks
double ArrangeView::playheadTick() const {
    return static_cast<double>(m_transport.position()) / samplesPerTick();
}

// Finds the clip under (trackIndex, tick), fills found and returns true on a hit
bool ArrangeView::hitTestClip(std::size_t trackIndex, int64_t tick, DraggedClip& found) const {
    const model::Track& track = m_arrangement.track(trackIndex);
    const double spt = samplesPerTick();

    for (std::size_t i = 0; i < track.midiClips.size(); ++i) {
        const auto& placement = track.midiClips[i];
        const int64_t endTick = placement.startTick + placement.clip.lengthTicks();
        if (tick >= placement.startTick && tick < endTick) {
            found = DraggedClip { ClipKind::Midi, trackIndex, i, placement.startTick };
            return true;
        }
    }

    for (std::size_t i = 0; i < track.audioClips.size(); ++i) {
        const auto& placement = track.audioClips[i];
        const auto lengthTicks = static_cast<int64_t>(static_cast<double>(placement.clip.activeLengthSamples()) / spt);
        const int64_t endTick = placement.startTick + lengthTicks;
        if (tick >= placement.startTick && tick < endTick) {
            found = DraggedClip { ClipKind::Audio, trackIndex, i, placement.startTick };
            return true;
        }
    }

    return false;
}

// True when x sits within kResizeHandlePixels of clip's right edge, always false for audio
bool ArrangeView::isNearResizeHandle(const DraggedClip& clip, int x) const {
    if (clip.kind != ClipKind::Midi) {
        return false;
    }

    const auto& midiClip = m_arrangement.track(clip.trackIndex).midiClips[clip.placementIndex].clip;
    const float rightEdgeX = tickToX(clip.originalStartTick + midiClip.lengthTicks());
    return std::abs(static_cast<float>(x) - rightEdgeX) <= static_cast<float>(kResizeHandlePixels);
}

// The snap unit resize clamps to, 240 (a step) rather than a beat when the division is Off,
// per this task's own contract, deliberately not the same "Beat when Off" convention P9 uses
int64_t ArrangeView::minimumResizeLengthTicks(model::SnapDivision division) const {
    return division == model::SnapDivision::Off
        ? model::snapUnitTicks(model::SnapDivision::Step) : model::snapUnitTicks(division);
}

// True when (kind, trackIndex, placementIndex) matches an entry in m_selection
bool ArrangeView::isSelected(ClipKind kind, std::size_t trackIndex, std::size_t placementIndex) const {
    return std::any_of(m_selection.begin(), m_selection.end(), [&](const ClipRef& ref) {
        return ref.kind == kind && ref.trackIndex == trackIndex && ref.placementIndex == placementIndex;
    });
}

// Selects every clip (both kinds, any track) intersecting the marquee rectangle, replacing
// the current selection; an empty sweep (a plain click that never dragged) clears it, since
// nothing can intersect a marquee whose start and end are the same empty-space point
void ArrangeView::finalizeClipMarquee() {
    const int64_t tickA = xToTick(m_clipMarqueeStart.x);
    const int64_t tickB = xToTick(m_clipMarqueeCurrent.x);
    const int64_t minTick = juce::jmin(tickA, tickB);
    const int64_t maxTick = juce::jmax(tickA, tickB);

    const std::size_t trackA = yToTrackIndex(m_clipMarqueeStart.y);
    const std::size_t trackB = yToTrackIndex(m_clipMarqueeCurrent.y);
    const std::size_t minTrack = juce::jmin(trackA, trackB);
    const std::size_t maxTrack = juce::jmax(trackA, trackB);

    const double spt = samplesPerTick();

    m_selection.clear();
    for (std::size_t t = minTrack; t <= maxTrack && t < m_arrangement.numTracks(); ++t) {
        const model::Track& track = m_arrangement.track(t);

        for (std::size_t p = 0; p < track.midiClips.size(); ++p) {
            const auto& placement = track.midiClips[p];
            const int64_t endTick = placement.startTick + placement.clip.lengthTicks();
            if (placement.startTick < maxTick && endTick > minTick) {
                m_selection.push_back(ClipRef { ClipKind::Midi, t, p });
            }
        }

        for (std::size_t p = 0; p < track.audioClips.size(); ++p) {
            const auto& placement = track.audioClips[p];
            const auto lengthTicks = static_cast<int64_t>(static_cast<double>(placement.clip.activeLengthSamples()) / spt);
            const int64_t endTick = placement.startTick + lengthTicks;
            if (placement.startTick < maxTick && endTick > minTick) {
                m_selection.push_back(ClipRef { ClipKind::Audio, t, p });
            }
        }
    }
}

// Fills outStartTick with the group-move preview position for (kind, trackIndex,
// placementIndex) and returns true, or returns false when it is not part of the drag
bool ArrangeView::findGroupPreviewTick(ClipKind kind, std::size_t trackIndex, std::size_t placementIndex,
                                       int64_t& outStartTick) const {
    for (const auto& member : m_dragGroupOriginal) {
        if (member.kind == kind && member.trackIndex == trackIndex && member.placementIndex == placementIndex) {
            const int64_t tickDelta = m_dragCurrentTick - m_draggedClip.originalStartTick;
            outStartTick = juce::jmax<int64_t>(0, member.originalStartTick + tickDelta);
            return true;
        }
    }
    return false;
}

// Draws the ruler, one lane per track, each clip as a block, and the playhead
void ArrangeView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    const int64_t span = visibleTickSpan();
    const int64_t barTicks = model::kTicksPerQuarter * 4;
    const int64_t firstBarTick = (m_scrollTick / barTicks) * barTicks;

    g.setColour(juce::Colours::grey.withAlpha(0.25f));
    for (int64_t tick = firstBarTick; tick < span; tick += barTicks) {
        const auto x = static_cast<int>(tickToX(tick));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
    }

    g.setColour(juce::Colours::white.withAlpha(0.8f));
    for (int64_t tick = firstBarTick; tick < span; tick += barTicks) {
        const auto x = static_cast<int>(tickToX(tick));
        const int barNumber = static_cast<int>(tick / barTicks) + 1;
        g.drawText(juce::String(barNumber), x + 2, 0, 40, kRulerHeight, juce::Justification::centredLeft);
    }

    const double spt = samplesPerTick();

    if (m_transport.loopEnabled()) {
        const auto loopStartTick = static_cast<int64_t>(static_cast<double>(m_transport.loopStart()) / spt);
        const auto loopEndTick = static_cast<int64_t>(static_cast<double>(m_transport.loopEnd()) / spt);
        const float x1 = tickToX(loopStartTick);
        const float x2 = tickToX(loopEndTick);
        g.setColour(juce::Colours::yellow.withAlpha(0.25f));
        g.fillRect(juce::Rectangle<float> { x1, 0.0f, x2 - x1, static_cast<float>(kRulerHeight) });
    }

    if (m_rulerDragging && m_rulerAnchorTick != m_rulerCurrentTick) {
        const int64_t rangeStart = juce::jmin(m_rulerAnchorTick, m_rulerCurrentTick);
        const int64_t rangeEnd = juce::jmax(m_rulerAnchorTick, m_rulerCurrentTick);
        const float x1 = tickToX(rangeStart);
        const float x2 = tickToX(rangeEnd);
        g.setColour(juce::Colours::yellow.withAlpha(0.35f));
        g.fillRect(juce::Rectangle<float> { x1, 0.0f, x2 - x1, static_cast<float>(kRulerHeight) });
    }

    const std::size_t numTracks = m_arrangement.numTracks();
    if (numTracks == 0) {
        return;
    }

    const float height = laneHeight();

    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    for (std::size_t i = 1; i < numTracks; ++i) {
        const auto y = kRulerHeight + static_cast<float>(i) * height;
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
    }

    for (std::size_t i = 0; i < numTracks; ++i) {
        const model::Track& track = m_arrangement.track(i);
        const auto y = kRulerHeight + static_cast<float>(i) * height;

        for (std::size_t p = 0; p < track.midiClips.size(); ++p) {
            const auto& placement = track.midiClips[p];
            int64_t startTick = placement.startTick;
            if (m_clipDragMode == ClipDragMode::Move) {
                findGroupPreviewTick(ClipKind::Midi, i, p, startTick);
            }

            const float x = tickToX(startTick);
            const float width = tickToX(startTick + placement.clip.lengthTicks()) - x;
            juce::Rectangle<float> r { x, y + 2.0f, juce::jmax(2.0f, width), height - 5.0f };
            g.setColour(juce::Colours::orange);
            g.fillRect(r);
            g.setColour(isSelected(ClipKind::Midi, i, p) ? juce::Colours::yellow : juce::Colours::orange.darker(0.8f));
            g.drawRect(r, isSelected(ClipKind::Midi, i, p) ? 2.5f : 1.5f);
        }

        for (std::size_t p = 0; p < track.audioClips.size(); ++p) {
            const auto& placement = track.audioClips[p];
            int64_t startTick = placement.startTick;
            if (m_clipDragMode == ClipDragMode::Move) {
                findGroupPreviewTick(ClipKind::Audio, i, p, startTick);
            }

            const auto lengthTicks = static_cast<int64_t>(static_cast<double>(placement.clip.activeLengthSamples()) / spt);
            const float x = tickToX(startTick);
            const float width = tickToX(startTick + lengthTicks) - x;
            juce::Rectangle<float> r { x, y + 2.0f, juce::jmax(2.0f, width), height - 5.0f };
            g.setColour(juce::Colours::steelblue);
            g.fillRect(r);
            g.setColour(isSelected(ClipKind::Audio, i, p) ? juce::Colours::yellow : juce::Colours::steelblue.darker(0.8f));
            g.drawRect(r, isSelected(ClipKind::Audio, i, p) ? 2.5f : 1.5f);
        }

        // Duplicate ghost: the original clip stays exactly where it is, this outline alone follows the drag
        if (m_clipDragMode == ClipDragMode::Duplicate && m_draggedClip.trackIndex == i) {
            int64_t ghostLengthTicks = 0;
            if (m_draggedClip.kind == ClipKind::Midi) {
                ghostLengthTicks = track.midiClips[m_draggedClip.placementIndex].clip.lengthTicks();
            } else {
                const auto& audioPlacement = track.audioClips[m_draggedClip.placementIndex];
                ghostLengthTicks = static_cast<int64_t>(static_cast<double>(audioPlacement.clip.activeLengthSamples()) / spt);
            }

            const float ghostX = tickToX(m_dragCurrentTick);
            const float ghostWidth = tickToX(m_dragCurrentTick + ghostLengthTicks) - ghostX;
            juce::Rectangle<float> ghost { ghostX, y + 2.0f, juce::jmax(2.0f, ghostWidth), height - 5.0f };
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.fillRect(ghost);
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawRect(ghost, 1.5f);
        }

        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawText(track.name, 4, static_cast<int>(y) + 2, 200, 14, juce::Justification::topLeft);
    }

    g.setColour(juce::Colours::white);
    const auto playheadX = tickToX(static_cast<int64_t>(playheadTick()));
    g.drawVerticalLine(static_cast<int>(playheadX), 0.0f, static_cast<float>(getHeight()));

    // Clip marquee, a translucent rectangle over the region being swept
    if (m_clipMarqueeActive) {
        const auto marqueeBounds = juce::Rectangle<int>(m_clipMarqueeStart, m_clipMarqueeCurrent);
        g.setColour(juce::Colours::lightblue.withAlpha(0.2f));
        g.fillRect(marqueeBounds);
        g.setColour(juce::Colours::lightblue.withAlpha(0.6f));
        g.drawRect(marqueeBounds, 1.0f);
    }
}

// Begins a move or resize drag if the click landed on a clip, opens a delete menu on right-click
void ArrangeView::mouseDown(const juce::MouseEvent& event) {
    m_mouseDownPosition = event.getPosition();
    m_hasDraggedBeyondThreshold = false;
    m_clipDragMode = ClipDragMode::None;

    if (event.y < kRulerHeight) {
        if (event.mods.isPopupMenu()) {
            showRulerMenu();
            return;
        }

        const int64_t tick = xToTick(event.x);
        m_rulerAnchorTick = model::snapTick(tick, snapDivision());
        m_rulerCurrentTick = m_rulerAnchorTick;
        m_rulerDragging = true;
        return;
    }

    if (m_arrangement.numTracks() == 0) {
        return;
    }

    const std::size_t trackIndex = yToTrackIndex(event.y);
    const int64_t tick = xToTick(event.x);

    DraggedClip found {};
    if (!hitTestClip(trackIndex, tick, found)) {
        if (!event.mods.isPopupMenu()) {
            m_clipMarqueeActive = true;
            m_clipMarqueeStart = event.getPosition();
            m_clipMarqueeCurrent = m_clipMarqueeStart;
        }
        return;
    }

    if (event.mods.isPopupMenu()) {
        showDeleteClipMenu(found);
        return;
    }

    m_draggedClip = found;

    if (event.mods.isCommandDown()) {
        m_dragCurrentTick = found.originalStartTick;
        m_clipDragMode = ClipDragMode::Duplicate;
        return;
    }

    if (isNearResizeHandle(found, event.x)) {
        m_dragOriginalLengthTicks = m_arrangement.track(found.trackIndex).midiClips[found.placementIndex].clip.lengthTicks();
        m_dragCurrentLengthTicks = m_dragOriginalLengthTicks;
        m_clipDragMode = ClipDragMode::Resize;
        return;
    }

    // Click rules: a click on an already selected clip keeps the whole selection so a group
    // drag can start, a click on an unselected clip selects only it
    if (!isSelected(found.kind, found.trackIndex, found.placementIndex)) {
        m_selection = { ClipRef { found.kind, found.trackIndex, found.placementIndex } };
    }

    m_dragCurrentTick = found.originalStartTick;
    m_clipDragMode = ClipDragMode::Move;

    m_dragGroupOriginal.clear();
    for (const ClipRef& ref : m_selection) {
        const int64_t originalStartTick = ref.kind == ClipKind::Midi
            ? m_arrangement.track(ref.trackIndex).midiClips[ref.placementIndex].startTick
            : m_arrangement.track(ref.trackIndex).audioClips[ref.placementIndex].startTick;
        m_dragGroupOriginal.push_back(DraggedClip { ref.kind, ref.trackIndex, ref.placementIndex, originalStartTick });
    }
}

// Continues a move drag (a visual preview only, committed on mouseUp) or a resize drag (which
// mutates the clip's length live, the gesture rule) once the mouse has moved past a small threshold
void ArrangeView::mouseDrag(const juce::MouseEvent& event) {
    if (m_rulerDragging) {
        const int64_t tick = xToTick(event.x);
        m_rulerCurrentTick = model::snapTick(tick, snapDivision());
        repaint();
        return;
    }

    if (m_clipMarqueeActive) {
        m_clipMarqueeCurrent = event.getPosition();
        repaint();
        return;
    }

    if (m_clipDragMode == ClipDragMode::None) {
        return;
    }

    if (event.getPosition().getDistanceFrom(m_mouseDownPosition) > kDragThresholdPixels) {
        m_hasDraggedBeyondThreshold = true;
    }
    if (!m_hasDraggedBeyondThreshold) {
        return;
    }

    if (m_clipDragMode == ClipDragMode::Resize) {
        const model::SnapDivision division = snapDivision();
        const int64_t rawLength = xToTick(event.x) - m_draggedClip.originalStartTick;
        const int64_t snappedLength = model::snapTick(rawLength, division);
        m_dragCurrentLengthTicks = juce::jmax(minimumResizeLengthTicks(division), snappedLength);

        m_arrangement.track(m_draggedClip.trackIndex).midiClips[m_draggedClip.placementIndex]
            .clip.setLengthTicks(m_dragCurrentLengthTicks);
    } else {
        const int64_t newTick = xToTick(event.x);
        m_dragCurrentTick = model::snapTick(juce::jmax<int64_t>(0, newTick), snapDivision());
    }

    repaint();
}

// Shows a left-right resize cursor when hovering a MIDI clip's right edge
void ArrangeView::mouseMove(const juce::MouseEvent& event) {
    if (m_arrangement.numTracks() == 0 || event.y < kRulerHeight) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const std::size_t trackIndex = yToTrackIndex(event.y);
    const int64_t tick = xToTick(event.x);

    DraggedClip found {};
    if (hitTestClip(trackIndex, tick, found) && isNearResizeHandle(found, event.x)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

// Finalizes a clip marquee, or issues the move/resize/duplicate clip command for a completed
// drag (resize already mutated the clip live, this just makes it undoable), or fires
// onMidiClipSelected for a plain click
void ArrangeView::mouseUp(const juce::MouseEvent&) {
    if (m_rulerDragging) {
        m_rulerDragging = false;

        if (m_rulerAnchorTick == m_rulerCurrentTick) {
            const auto samplePos = static_cast<SampleCount>(static_cast<double>(m_rulerAnchorTick) * samplesPerTick());
            m_transport.setPosition(samplePos);
        } else {
            const int64_t rangeStart = juce::jmin(m_rulerAnchorTick, m_rulerCurrentTick);
            const int64_t rangeEnd = juce::jmax(m_rulerAnchorTick, m_rulerCurrentTick);
            const auto sampleStart = static_cast<SampleCount>(static_cast<double>(rangeStart) * samplesPerTick());
            const auto sampleEnd = static_cast<SampleCount>(static_cast<double>(rangeEnd) * samplesPerTick());
            m_transport.setLoop(sampleStart, sampleEnd, true);
        }

        repaint();
        return;
    }

    if (m_clipMarqueeActive) {
        m_clipMarqueeActive = false;
        finalizeClipMarquee();
        repaint();
        return;
    }

    if (m_clipDragMode == ClipDragMode::None) {
        return;
    }

    if (m_hasDraggedBeyondThreshold) {
        if (m_clipDragMode == ClipDragMode::Duplicate) {
            if (m_draggedClip.kind == ClipKind::Midi) {
                const model::MidiClip source = m_arrangement.track(m_draggedClip.trackIndex)
                    .midiClips[m_draggedClip.placementIndex].clip;
                m_commandStack.perform(std::make_unique<model::AddMidiClipCommand>(m_arrangement,
                    m_draggedClip.trackIndex, model::MidiClipPlacement { m_dragCurrentTick, source }));
            } else {
                const model::AudioClip source = m_arrangement.track(m_draggedClip.trackIndex)
                    .audioClips[m_draggedClip.placementIndex].clip;
                m_commandStack.perform(std::make_unique<model::AddAudioClipCommand>(m_arrangement,
                    m_draggedClip.trackIndex, model::AudioClipPlacement { m_dragCurrentTick, source }));
            }
        } else if (m_clipDragMode == ClipDragMode::Resize) {
            m_commandStack.perform(std::make_unique<model::ResizeMidiClipCommand>(m_arrangement,
                m_draggedClip.trackIndex, m_draggedClip.placementIndex,
                m_dragOriginalLengthTicks, m_dragCurrentLengthTicks));
        } else {
            // Group move: one CompositeCommand for the whole selection, sharing the reference
            // clip's tick delta. Moves re-sort placements by tick same as note commands re-sort
            // by tick, so the selection is rebuilt from each child's own resulting index rather
            // than assumed to still match the indices captured at mouseDown
            auto composite = std::make_unique<model::CompositeCommand>();
            const int64_t tickDelta = m_dragCurrentTick - m_draggedClip.originalStartTick;

            // Moving the highest original index in a track+kind bucket first (same trap the
            // delete gesture pins descending order for) means an unselected clip sitting between
            // two selected ones can never invalidate a not-yet-executed sibling's captured
            // index: everything with a lower original index is still exactly where it started
            // until its own turn comes. A uniform delta cannot reorder the selected members
            // relative to each other, only relative to what is between and around them, and
            // this ordering is what keeps that safe
            std::vector<DraggedClip> processingOrder = m_dragGroupOriginal;
            std::sort(processingOrder.begin(), processingOrder.end(), [](const DraggedClip& a, const DraggedClip& b) {
                if (a.trackIndex != b.trackIndex) {
                    return a.trackIndex < b.trackIndex;
                }
                if (a.kind != b.kind) {
                    return a.kind < b.kind;
                }
                return a.placementIndex > b.placementIndex;
            });

            struct TrackedMove {
                ClipKind kind;
                std::size_t trackIndex;
                model::MoveMidiClipCommand* midi = nullptr;
                model::MoveAudioClipCommand* audio = nullptr;
            };
            std::vector<TrackedMove> tracked;
            tracked.reserve(processingOrder.size());

            for (const auto& member : processingOrder) {
                const int64_t newTick = juce::jmax<int64_t>(0, member.originalStartTick + tickDelta);
                if (member.kind == ClipKind::Midi) {
                    auto move = std::make_unique<model::MoveMidiClipCommand>(
                        m_arrangement, member.trackIndex, member.placementIndex, newTick);
                    tracked.push_back(TrackedMove { ClipKind::Midi, member.trackIndex, move.get(), nullptr });
                    composite->add(std::move(move));
                } else {
                    auto move = std::make_unique<model::MoveAudioClipCommand>(
                        m_arrangement, member.trackIndex, member.placementIndex, newTick);
                    tracked.push_back(TrackedMove { ClipKind::Audio, member.trackIndex, nullptr, move.get() });
                    composite->add(std::move(move));
                }
            }

            m_commandStack.perform(std::move(composite));

            m_selection.clear();
            for (const TrackedMove& move : tracked) {
                const std::size_t resultIndex = move.kind == ClipKind::Midi
                    ? move.midi->placementIndex() : move.audio->placementIndex();
                m_selection.push_back(ClipRef { move.kind, move.trackIndex, resultIndex });
            }
        }
    } else if (m_clipDragMode != ClipDragMode::Duplicate && m_draggedClip.kind == ClipKind::Midi && onMidiClipSelected) {
        onMidiClipSelected(m_draggedClip.trackIndex, m_draggedClip.placementIndex);
    }

    m_clipDragMode = ClipDragMode::None;
    repaint();
}

// Creates a new 4-bar MIDI clip on an empty MIDI lane, snapped to the bar
void ArrangeView::mouseDoubleClick(const juce::MouseEvent& event) {
    if (m_arrangement.numTracks() == 0 || event.y < kRulerHeight) {
        return;
    }

    const std::size_t trackIndex = yToTrackIndex(event.y);
    const model::Track& track = m_arrangement.track(trackIndex);

    if (track.kind != model::TrackKind::Midi) {
        return;
    }

    const int64_t tick = xToTick(event.x);

    DraggedClip found {};
    if (hitTestClip(trackIndex, tick, found)) {
        return;
    }

    const int64_t snappedTick = model::snapTickFloor(tick, snapDivision());

    model::MidiClip clip;
    clip.setLengthTicks(model::kTicksPerQuarter * 16);

    m_commandStack.perform(std::make_unique<model::AddMidiClipCommand>(
        m_arrangement, trackIndex, model::MidiClipPlacement { snappedTick, clip }));

    repaint();
}

// Ctrl+wheel zooms around the cursor, plain wheel scrolls horizontally
void ArrangeView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (event.mods.isCtrlDown() || event.mods.isCommandDown()) {
        const int64_t tickUnderCursor = xToTick(event.x);
        m_zoom = juce::jlimit(kMinZoom, kMaxZoom, m_zoom * (1.0 + static_cast<double>(wheel.deltaY)));

        const double newSpan = zoomedVisibleSpan();
        const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
        m_scrollTick = tickUnderCursor - static_cast<int64_t>(ratio * newSpan);
    } else {
        const double span = zoomedVisibleSpan();
        m_scrollTick += static_cast<int64_t>(-static_cast<double>(wheel.deltaY) * span / 4.0);
    }

    clampScroll();
    repaint();
}

// Accepts a drag hovering any .wav file
bool ArrangeView::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& file : files) {
        if (file.endsWithIgnoreCase(".wav")) {
            return true;
        }
    }

    return false;
}

// Fires onAudioFileDropped with the drop lane and snapped tick for each dropped .wav
void ArrangeView::filesDropped(const juce::StringArray& files, int x, int y) {
    if (m_arrangement.numTracks() == 0 || y < kRulerHeight) {
        return;
    }

    const std::size_t trackIndex = yToTrackIndex(y);
    const int64_t tick = xToTick(x);
    const int64_t snappedTick = model::snapTickFloor(tick, snapDivision());

    for (const auto& file : files) {
        if (file.endsWithIgnoreCase(".wav") && onAudioFileDropped) {
            onAudioFileDropped(file, trackIndex, snappedTick);
        }
    }
}

// Accepts a drag whose description matches the browser's "howl-sample" tag
bool ArrangeView::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    return dragSourceDetails.description == juce::var("howl-sample");
}

// Fires onAudioFileDropped for the browser's currently selected file, same lane/tick math as filesDropped
void ArrangeView::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    if (m_arrangement.numTracks() == 0 || dragSourceDetails.localPosition.y < kRulerHeight) {
        return;
    }

    if (!browserFileProvider || !onAudioFileDropped) {
        return;
    }

    const juce::File file = browserFileProvider();
    if (!file.existsAsFile()) {
        return;
    }

    const std::size_t trackIndex = yToTrackIndex(dragSourceDetails.localPosition.y);
    const int64_t tick = xToTick(dragSourceDetails.localPosition.x);
    const int64_t snappedTick = model::snapTickFloor(tick, snapDivision());

    onAudioFileDropped(file.getFullPathName(), trackIndex, snappedTick);
}

// Opens a "Delete Clip" popup for the given clip, with warp toggle and BPM entry for audio clips
void ArrangeView::showDeleteClipMenu(const DraggedClip& target) {
    juce::PopupMenu menu;
    menu.addItem(1, "Delete Clip");

    if (target.kind == ClipKind::Audio) {
        const model::AudioClip& clip = m_arrangement.track(target.trackIndex).audioClips[target.placementIndex].clip;
        menu.addItem(2, clip.warpEnabled() ? "Warp: On" : "Warp: Off", true, clip.warpEnabled());
        menu.addItem(3, "Set Original BPM...");
    }

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, target](int result) {
        if (result == 1) {
            if (target.kind == ClipKind::Midi) {
                m_commandStack.perform(std::make_unique<model::RemoveMidiClipCommand>(
                    m_arrangement, target.trackIndex, target.placementIndex));
            } else {
                m_commandStack.perform(std::make_unique<model::RemoveAudioClipCommand>(
                    m_arrangement, target.trackIndex, target.placementIndex));
            }
            repaint();
        } else if (result == 2 && target.kind == ClipKind::Audio) {
            model::AudioClip& clip = m_arrangement.track(target.trackIndex).audioClips[target.placementIndex].clip;
            clip.setWarpEnabled(!clip.warpEnabled());
            if (onWarpChanged) {
                onWarpChanged();
            }
        } else if (result == 3 && target.kind == ClipKind::Audio) {
            showSetOriginalBpmDialog(target);
        }
    });
}

// Opens an async "Set Original BPM..." dialog for the given audio clip
void ArrangeView::showSetOriginalBpmDialog(const DraggedClip& target) {
    const model::AudioClip& clip = m_arrangement.track(target.trackIndex).audioClips[target.placementIndex].clip;

    auto* window = new juce::AlertWindow("Set Original BPM",
        "Tempo the source material was recorded at:", juce::AlertWindow::NoIcon);
    window->addTextEditor("bpm", juce::String(clip.originalBpm(), 1));
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create([this, target, window](int result) {
        if (result == 1) {
            const double parsed = window->getTextEditorContents("bpm").getDoubleValue();
            const double clamped = juce::jlimit(20.0, 300.0, parsed);
            m_arrangement.track(target.trackIndex).audioClips[target.placementIndex].clip.setOriginalBpm(clamped);

            if (onWarpChanged) {
                onWarpChanged();
            }
        }
    }), true);
}

// Toggles the transport between play and stop on space, fires onMixerRequested on M,
// rewinds to 0 on Home
bool ArrangeView::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey) {
        if (m_transport.isPlaying()) {
            m_transport.stop();
        } else {
            m_transport.play();
        }
        return true;
    }

    if (key == juce::KeyPress('M')) {
        if (onMixerRequested) {
            onMixerRequested();
        }
        return true;
    }

    if (key == juce::KeyPress::homeKey) {
        m_transport.setPosition(0);
        repaint();
        return true;
    }

    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && !m_selection.empty()) {
        // Same trap as a group move: removing a placement shifts every later index in its
        // own track+kind array, so descending order within each bucket is the only order
        // that keeps every not-yet-removed sibling's captured index valid
        std::vector<ClipRef> descending = m_selection;
        std::sort(descending.begin(), descending.end(), [](const ClipRef& a, const ClipRef& b) {
            if (a.trackIndex != b.trackIndex) {
                return a.trackIndex < b.trackIndex;
            }
            if (a.kind != b.kind) {
                return a.kind < b.kind;
            }
            return a.placementIndex > b.placementIndex;
        });

        auto composite = std::make_unique<model::CompositeCommand>();
        for (const ClipRef& ref : descending) {
            if (ref.kind == ClipKind::Midi) {
                composite->add(std::make_unique<model::RemoveMidiClipCommand>(m_arrangement, ref.trackIndex, ref.placementIndex));
            } else {
                composite->add(std::make_unique<model::RemoveAudioClipCommand>(m_arrangement, ref.trackIndex, ref.placementIndex));
            }
        }

        m_commandStack.perform(std::move(composite));
        m_selection.clear();
        repaint();
        return true;
    }

    return false;
}

// Opens a one-item "Loop: On/Off" menu for the ruler, toggles looping without moving the region
void ArrangeView::showRulerMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Loop: On/Off", true, m_transport.loopEnabled());

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result == 1) {
            m_transport.setLoop(m_transport.loopStart(), m_transport.loopEnd(), !m_transport.loopEnabled());
            repaint();
        }
    });
}

} // namespace howl::ui
