// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows tracks as lanes and clips as blocks, edited through the command stack

#pragma once

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/CommandStack.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>

namespace howl::ui {

// v1 scope: drag a clip's body to move it, issuing one move-clip command per
// drag gesture, not one per mouse-move. Editing is only tested with the
// transport stopped, same caveat as PianoRoll. Clicking a MIDI clip without
// dragging fires onMidiClipSelected, actually opening a PianoRoll for it is
// wired in P4-T7
class ArrangeView : public juce::Component, private juce::Timer {
public:
    // Stores references to the arrangement, transport, and command stack, starts the playhead timer
    ArrangeView(model::Arrangement& arrangement, engine::Transport& transport,
                model::CommandStack& commandStack, double sampleRate);

    // Stops the playhead timer
    ~ArrangeView() override;

    // Draws one lane per track, each clip as a block, and the playhead
    void paint(juce::Graphics& g) override;

    // Begins a move drag if the click landed on a clip
    void mouseDown(const juce::MouseEvent& event) override;

    // Continues a move drag once the mouse has moved past a small threshold
    void mouseDrag(const juce::MouseEvent& event) override;

    // Issues the move-clip command for a completed drag, or fires onMidiClipSelected for a plain click
    void mouseUp(const juce::MouseEvent& event) override;

    // Toggles the transport between play and stop on space, fires onMixerRequested on M
    bool keyPressed(const juce::KeyPress& key) override;

    // Called with (trackIndex, placementIndex) when a MIDI clip is clicked without dragging
    std::function<void(std::size_t, std::size_t)> onMidiClipSelected;

    // Called when M is pressed to open the mixer
    std::function<void()> onMixerRequested;

private:
    static constexpr int64_t kMinimumVisibleTicks = model::kTicksPerQuarter * 16; // 4 bars at 4/4
    static constexpr int kDragThresholdPixels = 4;

    enum class ClipKind {
        Midi,
        Audio
    };

    struct DraggedClip {
        ClipKind kind;
        std::size_t trackIndex;
        std::size_t placementIndex;
        int64_t originalStartTick;
    };

    // Repaints so the playhead position stays current during playback
    void timerCallback() override;

    // Samples per tick from the transport's current tempo and this view's sample rate
    double samplesPerTick() const;

    // Ticks spanned by the visible timeline, covers every placed clip plus a sensible minimum
    int64_t visibleTickSpan() const;

    // Converts a pixel x position to a tick, clamped to the visible span
    int64_t xToTick(int x) const;

    // Converts a tick to a pixel x position
    float tickToX(int64_t tick) const;

    // Returns the height of one track lane
    float laneHeight() const;

    // Converts a pixel y position to a track index, clamped to numTracks() - 1
    std::size_t yToTrackIndex(int y) const;

    // Returns the current transport position converted to ticks
    double playheadTick() const;

    // Finds the clip under (trackIndex, tick), fills found and returns true on a hit
    bool hitTestClip(std::size_t trackIndex, int64_t tick, DraggedClip& found) const;

    model::Arrangement& m_arrangement;
    engine::Transport& m_transport;
    model::CommandStack& m_commandStack;
    double m_sampleRate;

    bool m_dragging = false;
    DraggedClip m_draggedClip {};
    int64_t m_dragCurrentTick = 0;
    juce::Point<int> m_mouseDownPosition;
    bool m_hasDraggedBeyondThreshold = false;
};

} // namespace howl::ui
