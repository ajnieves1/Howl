// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: shows and edits a MidiClip, draws the transport playhead

#pragma once

#include "engine/Transport.h"
#include "model/MidiClip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>

namespace hearth::ui {

// v1 scope: click empty space to add a note, click an existing note without
// dragging to delete it, drag its body to move it or its right edge to
// resize it. Editing is only tested with the transport stopped, edits made
// during playback are not specially guarded against but are not this task's
// concern
class PianoRoll : public juce::Component, private juce::Timer {
public:
    // Stores references to the clip and transport, starts the playhead timer
    PianoRoll(model::MidiClip& clip, engine::Transport& transport, double sampleRate);

    // Stops the playhead timer
    ~PianoRoll() override;

    // Draws the key grid, notes, and playhead
    void paint(juce::Graphics& g) override;

    // Begins adding a note, or begins a move or resize drag on an existing note
    void mouseDown(const juce::MouseEvent& event) override;

    // Continues a move or resize drag once the mouse has moved past a small threshold
    void mouseDrag(const juce::MouseEvent& event) override;

    // Deletes the note if mouseDown started a drag that never moved
    void mouseUp(const juce::MouseEvent& event) override;

    // Toggles the transport between play and stop when space is pressed
    bool keyPressed(const juce::KeyPress& key) override;

private:
    static constexpr int kLowestKey = 36; // C2
    static constexpr int kHighestKey = 96; // C7
    static constexpr int kNumKeys = kHighestKey - kLowestKey + 1;
    static constexpr int64_t kDefaultNoteLengthTicks = model::kTicksPerQuarter;
    static constexpr int kResizeHandlePixels = 6;
    static constexpr int kDragThresholdPixels = 4;

    enum class DragMode {
        None,
        Move,
        Resize
    };

    // Repaints so the playhead position stays current during playback
    void timerCallback() override;

    // Ticks spanned by the visible grid, at least 4 beats even for an empty clip
    int64_t visibleTickSpan() const;

    // Converts a pixel x position to a tick, clamped to the visible span
    int64_t xToTick(int x) const;

    // Converts a pixel y position to a MIDI key, clamped to the visible range
    int yToKey(int y) const;

    // Converts a tick to a pixel x position
    float tickToX(int64_t tick) const;

    // Converts a MIDI key to the y position of the top of its row
    float keyToY(int key) const;

    // Returns the index of the note at (tick, key), or -1 if none
    int hitTestNote(int64_t tick, int key) const;

    // Returns the current transport position converted to ticks
    double playheadTick() const;

    model::MidiClip& m_clip;
    engine::Transport& m_transport;
    double m_sampleRate;

    DragMode m_dragMode = DragMode::None;
    int m_dragNoteIndex = -1;
    int64_t m_dragTickOffset = 0;
    juce::Point<int> m_mouseDownPosition;
    bool m_hasDraggedBeyondThreshold = false;
};

} // namespace hearth::ui
