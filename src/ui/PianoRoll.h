// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows and edits a MidiClip resolved from a ClipAddress, draws the transport playhead

#pragma once

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/Commands.h"
#include "model/Session.h"
#include "model/SnapGrid.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

namespace howl::ui {

// v1 scope: click empty space to add a note, click an existing note without dragging to
// delete it, drag its body to move it or its right edge to resize it, both snapped to the
// global division. Editing is only tested with the transport stopped, edits made during
// playback are not specially guarded against but are not this task's concern. Every edit
// resolves its clip fresh through a ClipAddress rather than holding a MidiClip& directly,
// so the view stays safe across container reallocation; an address that stops resolving
// (the track, scene, or clip it pointed at is gone) draws an empty grid and ignores mice
class PianoRoll : public juce::Component, private juce::Timer {
public:
    // Stores references, the clip address, and the snap division provider, starts the playhead timer
    PianoRoll(model::Arrangement& arrangement, model::Session& session, model::ClipAddress address,
              model::CommandStack& commandStack, engine::Transport& transport, double sampleRate,
              std::function<model::SnapDivision()> snapProvider);

    // Stops the playhead timer
    ~PianoRoll() override;

    // Draws the key grid, notes, and playhead, an empty grid when the address does not resolve
    void paint(juce::Graphics& g) override;

    // Begins adding a note (performs AddNoteCommand), or begins a move or resize drag
    void mouseDown(const juce::MouseEvent& event) override;

    // Continues a move or resize drag once the mouse has moved past a small threshold
    void mouseDrag(const juce::MouseEvent& event) override;

    // Performs ReplaceNotesCommand for a completed drag, or RemoveNoteCommand for a plain click
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

    // Resolves the addressed clip fresh, nullptr when it no longer resolves
    model::MidiClip* resolveClip() const;

    // Ticks spanned by the visible grid, at least 4 beats even for an empty or unresolved clip
    int64_t visibleTickSpan(int64_t clipLengthTicks) const;

    // Converts a pixel x position to a tick, clamped to the visible span, unsnapped
    int64_t xToTick(int x, int64_t clipLengthTicks) const;

    // Converts a pixel y position to a MIDI key, clamped to the visible range
    int yToKey(int y) const;

    // Converts a tick to a pixel x position
    float tickToX(int64_t tick, int64_t clipLengthTicks) const;

    // Converts a MIDI key to the y position of the top of its row
    float keyToY(int key) const;

    // Returns the index of the note at (tick, key), or -1 if none, hit-testing is unsnapped
    int hitTestNote(const model::MidiClip& clip, int64_t tick, int key) const;

    // Returns the current transport position converted to ticks
    double playheadTick() const;

    // Returns the current global snap division, Step when no provider is set
    model::SnapDivision snapDivision() const;

    model::Arrangement& m_arrangement;
    model::Session& m_session;
    model::ClipAddress m_address;
    model::CommandStack& m_commandStack;
    engine::Transport& m_transport;
    double m_sampleRate;
    std::function<model::SnapDivision()> m_snapProvider;

    DragMode m_dragMode = DragMode::None;
    int m_dragNoteIndex = -1;
    int64_t m_dragTickOffset = 0;
    // The note's value at mouseDown, before any live drag mutation, the command's "before"
    model::Note m_dragOriginalNote {};
    juce::Point<int> m_mouseDownPosition;
    bool m_hasDraggedBeyondThreshold = false;
};

} // namespace howl::ui
