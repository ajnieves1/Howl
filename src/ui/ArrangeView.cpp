// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows tracks as lanes and clips as blocks, edited through the command stack

#include "ui/ArrangeView.h"

#include "model/Commands.h"

#include <memory>

namespace howl::ui {

// Stores references to the arrangement, transport, and command stack, starts the playhead timer
ArrangeView::ArrangeView(model::Arrangement& arrangement, engine::Transport& transport,
                          model::CommandStack& commandStack, double sampleRate)
    : m_arrangement(arrangement)
    , m_transport(transport)
    , m_commandStack(commandStack)
    , m_sampleRate(sampleRate)
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
            const auto lengthTicks = static_cast<int64_t>(static_cast<double>(placement.clip.lengthSamples()) / spt);
            const int64_t endTick = placement.startTick + lengthTicks;
            maxEndTick = endTick > maxEndTick ? endTick : maxEndTick;
        }
    }

    return maxEndTick + model::kTicksPerQuarter * 4;
}

// Converts a pixel x position to a tick, clamped to the visible span
int64_t ArrangeView::xToTick(int x) const {
    const int64_t span = visibleTickSpan();
    const float ratio = juce::jlimit(0.0f, 1.0f, static_cast<float>(x) / static_cast<float>(getWidth()));
    return static_cast<int64_t>(ratio * static_cast<float>(span));
}

// Converts a tick to a pixel x position
float ArrangeView::tickToX(int64_t tick) const {
    const int64_t span = visibleTickSpan();
    return static_cast<float>(tick) / static_cast<float>(span) * static_cast<float>(getWidth());
}

// Returns the height of one track lane
float ArrangeView::laneHeight() const {
    const std::size_t numTracks = m_arrangement.numTracks();
    if (numTracks == 0) {
        return static_cast<float>(getHeight());
    }
    return static_cast<float>(getHeight()) / static_cast<float>(numTracks);
}

// Converts a pixel y position to a track index, clamped to numTracks() - 1
std::size_t ArrangeView::yToTrackIndex(int y) const {
    const std::size_t numTracks = m_arrangement.numTracks();
    if (numTracks == 0) {
        return 0;
    }

    const float height = laneHeight();
    const int index = static_cast<int>(static_cast<float>(y) / height);
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
        const auto lengthTicks = static_cast<int64_t>(static_cast<double>(placement.clip.lengthSamples()) / spt);
        const int64_t endTick = placement.startTick + lengthTicks;
        if (tick >= placement.startTick && tick < endTick) {
            found = DraggedClip { ClipKind::Audio, trackIndex, i, placement.startTick };
            return true;
        }
    }

    return false;
}

// Draws one lane per track, each clip as a block, and the playhead
void ArrangeView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    const std::size_t numTracks = m_arrangement.numTracks();
    if (numTracks == 0) {
        return;
    }

    const float height = laneHeight();
    const double spt = samplesPerTick();

    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    for (std::size_t i = 1; i < numTracks; ++i) {
        const auto y = static_cast<float>(i) * height;
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
    }

    g.setColour(juce::Colours::grey.withAlpha(0.25f));
    const int64_t span = visibleTickSpan();
    for (int64_t tick = 0; tick < span; tick += model::kTicksPerQuarter * 4) {
        const auto x = static_cast<int>(tickToX(tick));
        g.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
    }

    for (std::size_t i = 0; i < numTracks; ++i) {
        const model::Track& track = m_arrangement.track(i);
        const auto y = static_cast<float>(i) * height;

        for (std::size_t p = 0; p < track.midiClips.size(); ++p) {
            const auto& placement = track.midiClips[p];
            int64_t startTick = placement.startTick;
            if (m_dragging && m_draggedClip.kind == ClipKind::Midi
                && m_draggedClip.trackIndex == i && m_draggedClip.placementIndex == p) {
                startTick = m_dragCurrentTick;
            }

            const float x = tickToX(startTick);
            const float width = tickToX(startTick + placement.clip.lengthTicks()) - x;
            juce::Rectangle<float> r { x, y + 2.0f, juce::jmax(2.0f, width), height - 5.0f };
            g.setColour(juce::Colours::orange);
            g.fillRect(r);
            g.setColour(juce::Colours::orange.darker(0.8f));
            g.drawRect(r, 1.5f);
        }

        for (std::size_t p = 0; p < track.audioClips.size(); ++p) {
            const auto& placement = track.audioClips[p];
            int64_t startTick = placement.startTick;
            if (m_dragging && m_draggedClip.kind == ClipKind::Audio
                && m_draggedClip.trackIndex == i && m_draggedClip.placementIndex == p) {
                startTick = m_dragCurrentTick;
            }

            const auto lengthTicks = static_cast<int64_t>(static_cast<double>(placement.clip.lengthSamples()) / spt);
            const float x = tickToX(startTick);
            const float width = tickToX(startTick + lengthTicks) - x;
            juce::Rectangle<float> r { x, y + 2.0f, juce::jmax(2.0f, width), height - 5.0f };
            g.setColour(juce::Colours::steelblue);
            g.fillRect(r);
            g.setColour(juce::Colours::steelblue.darker(0.8f));
            g.drawRect(r, 1.5f);
        }

        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawText(track.name, 4, static_cast<int>(y) + 2, 200, 14, juce::Justification::topLeft);
    }

    g.setColour(juce::Colours::white);
    const auto playheadX = tickToX(static_cast<int64_t>(playheadTick()));
    g.drawVerticalLine(static_cast<int>(playheadX), 0.0f, static_cast<float>(getHeight()));
}

// Begins a move drag if the click landed on a clip
void ArrangeView::mouseDown(const juce::MouseEvent& event) {
    m_mouseDownPosition = event.getPosition();
    m_hasDraggedBeyondThreshold = false;
    m_dragging = false;

    if (m_arrangement.numTracks() == 0) {
        return;
    }

    const std::size_t trackIndex = yToTrackIndex(event.y);
    const int64_t tick = xToTick(event.x);

    DraggedClip found {};
    if (hitTestClip(trackIndex, tick, found)) {
        m_draggedClip = found;
        m_dragCurrentTick = found.originalStartTick;
        m_dragging = true;
    }
}

// Continues a move drag once the mouse has moved past a small threshold
void ArrangeView::mouseDrag(const juce::MouseEvent& event) {
    if (!m_dragging) {
        return;
    }

    if (event.getPosition().getDistanceFrom(m_mouseDownPosition) > kDragThresholdPixels) {
        m_hasDraggedBeyondThreshold = true;
    }
    if (!m_hasDraggedBeyondThreshold) {
        return;
    }

    const int64_t newTick = xToTick(event.x);
    m_dragCurrentTick = newTick < 0 ? 0 : newTick;
    repaint();
}

// Issues the move-clip command for a completed drag, or fires onMidiClipSelected for a plain click
void ArrangeView::mouseUp(const juce::MouseEvent&) {
    if (!m_dragging) {
        return;
    }

    if (m_hasDraggedBeyondThreshold) {
        if (m_draggedClip.kind == ClipKind::Midi) {
            m_commandStack.perform(std::make_unique<model::MoveMidiClipCommand>(
                m_arrangement, m_draggedClip.trackIndex, m_draggedClip.placementIndex, m_dragCurrentTick));
        } else {
            m_commandStack.perform(std::make_unique<model::MoveAudioClipCommand>(
                m_arrangement, m_draggedClip.trackIndex, m_draggedClip.placementIndex, m_dragCurrentTick));
        }
    } else if (m_draggedClip.kind == ClipKind::Midi && onMidiClipSelected) {
        onMidiClipSelected(m_draggedClip.trackIndex, m_draggedClip.placementIndex);
    }

    m_dragging = false;
    repaint();
}

// Toggles the transport between play and stop on space, fires onMixerRequested on M
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

    return false;
}

} // namespace howl::ui
