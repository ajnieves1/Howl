// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows and edits a MidiClip resolved from a ClipAddress, draws the transport playhead

#pragma once

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/Commands.h"
#include "model/Pattern.h"
#include "model/Session.h"
#include "model/SnapGrid.h"
#include "ui/EditTool.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

namespace howl::ui {

// v1 scope: click empty space to add a note, drag its body to move it (dragging any selected
// note moves the whole selection) or its right edge to resize it, both snapped to the global
// division. Ctrl+drag an empty stretch of grid marquees a selection, Ctrl+Shift+drag adds to
// it; Shift+click toggles one note; Delete/Backspace removes the selection; arrow keys nudge
// or transpose it; Ctrl+D duplicates it. Editing is only tested with the transport stopped,
// edits made during playback are not specially guarded against but are not this task's
// concern. Every edit resolves its clip fresh through a ClipAddress rather than holding a
// MidiClip& directly, so the view stays safe across container reallocation; an address that
// stops resolving (the track, scene, or clip it pointed at is gone) draws an empty grid and
// ignores mice and keys
class PianoRoll : public juce::Component, private juce::Timer {
public:
    // Stores references, the clip address, and the snap division provider, starts the playhead timer
    PianoRoll(model::Arrangement& arrangement, model::Session& session, model::PatternBank& patterns,
              model::ClipAddress address, model::CommandStack& commandStack, engine::Transport& transport,
              double sampleRate, std::function<model::SnapDivision()> snapProvider);

    // Stops the playhead timer
    ~PianoRoll() override;

    // Draws the piano key gutter, key grid, notes, playhead, velocity lane, selection
    // borders, and marquee
    void paint(juce::Graphics& g) override;

    // Centers the view on the clip's notes (C5 when empty) the first time a size arrives,
    // and re-clamps the vertical scroll on any later resize
    void resized() override;

    // Plain wheel scrolls keys, Ctrl wheel zooms time around the cursor, Shift wheel scrolls
    // time, Alt wheel zooms the key height
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // Alt+click on a note performs SplitNoteCommand at the snapped click tick, otherwise
    // begins adding a note, a move/resize/velocity drag, a marquee, or updates selection
    void mouseDown(const juce::MouseEvent& event) override;

    // Continues a drag or a marquee once the mouse has moved past a small threshold
    void mouseDrag(const juce::MouseEvent& event) override;

    // Shows an I-beam cursor while Alt is held over a note, the slice gesture's cue
    void mouseMove(const juce::MouseEvent& event) override;

    // Finalizes a marquee selection, or performs one ReplaceNotesCommand for a completed drag
    void mouseUp(const juce::MouseEvent& event) override;

    // Space toggles play/stop; with a non-empty selection: Delete/Backspace removes it, arrow
    // keys nudge or transpose it, Ctrl+D duplicates it
    bool keyPressed(const juce::KeyPress& key) override;

    // Provides the selected edit tool, Draw when nothing is wired
    std::function<EditTool()> toolProvider;

private:
    // The full MIDI range. A narrower range silently drops notes outside it, which hid the
    // low notes of imported MIDI files entirely. The grid scrolls and the view centres on the
    // clip's own notes, so the extra range costs nothing on screen
    static constexpr int kLowestKey = 0;
    static constexpr int kHighestKey = 127;
    static constexpr int kNumKeys = kHighestKey - kLowestKey + 1;
    static constexpr int kKeyboardWidth = 84;
    static constexpr float kBaseKeyRowHeight = 12.0f; // row height at vertical zoom 1
    static constexpr int64_t kDefaultNoteLengthTicks = model::kTicksPerQuarter;
    static constexpr int kResizeHandlePixels = 6;
    static constexpr int kDragThresholdPixels = 4;
    static constexpr int kDefaultVelocityLaneHeight = 90;
    static constexpr int kMinVelocityLaneHeight = 40;
    static constexpr int kVelocityDividerZone = 5; // pixels above the lane that grab the divider
    static constexpr int kVelocityBarWidth = 5;
    static constexpr int kVelocityHitPixels = 6;
    static constexpr float kMinHorizontalZoom = 1.0f; // 1 fits the whole clip to the grid width
    static constexpr float kMaxHorizontalZoom = 48.0f;
    static constexpr float kMinVerticalZoom = 1.0f;
    static constexpr float kMaxVerticalZoom = 4.0f;

    enum class DragMode {
        None,
        Move,
        Resize,
        Velocity
    };

    // Repaints so the playhead position stays current during playback
    void timerCallback() override;

    // The selected edit tool, Draw when no provider is wired
    EditTool currentTool() const;

    // Adds one note at the snapped cell under (tick, key) unless one is already there, used by
    // the Draw click and by every step of a Paint stroke
    void paintNoteAt(int64_t tick, int key);

    // Resolves the addressed clip fresh, nullptr when it no longer resolves
    model::MidiClip* resolveClip() const;

    // Height of the key grid, the component's height less the velocity lane
    float keyGridHeight() const;

    // Row height for one key, never smaller than the height that fits every key into the grid
    // so the grid always fills, and larger when the vertical zoom is raised
    float rowHeight() const;

    // Content width of the grid in pixels at the current horizontal zoom
    float contentWidth() const;

    // Clamps the horizontal scroll into range, applies it, repaints on change
    void applyHorizontalScroll(int requested);

    // Ticks spanned by the visible grid, at least 4 beats even for an empty or unresolved clip
    int64_t visibleTickSpan(int64_t clipLengthTicks) const;

    // Converts a pixel x position to a tick, clamped to the visible span, unsnapped
    int64_t xToTick(int x, int64_t clipLengthTicks) const;

    // Converts a pixel y position to a MIDI key, clamped to the visible range
    int yToKey(int y) const;

    // Converts a tick to a pixel x position
    float tickToX(int64_t tick, int64_t clipLengthTicks) const;

    // Converts a MIDI key to the y position of the top of its row, in the current scroll
    float keyToY(int key) const;

    // Clamps requested into the valid vertical scroll range, applies it, repaints on change
    void applyVerticalScroll(int requested);

    // Returns the index of the note at (tick, key), or -1 if none, hit-testing is unsnapped
    int hitTestNote(const model::MidiClip& clip, int64_t tick, int key) const;

    // Returns the index of the note whose velocity bar is nearest x within kVelocityHitPixels,
    // or -1 if none; notes sharing a start tick break the tie toward the higher key
    int hitTestVelocityBar(const model::MidiClip& clip, int x) const;

    // Returns the current transport position converted to ticks
    double playheadTick() const;

    // Returns the current global snap division, Step when no provider is set
    model::SnapDivision snapDivision() const;

    // The division's own unit, Beat's unit when the division is Off (Off has no unit of its own)
    int64_t effectiveUnitTicks(model::SnapDivision division) const;

    // True when note matches an entry in m_selection by exact field value
    bool isSelected(const model::Note& note) const;

    // Adds note to the selection if absent, removes it if present
    void toggleSelection(const model::Note& note);

    // Selects every note intersecting the marquee rectangle, replacing or adding to the
    // current selection depending on whether Shift was held when the marquee began
    void finalizeMarquee();

    model::Arrangement& m_arrangement;
    model::Session& m_session;
    model::PatternBank& m_patterns;
    model::ClipAddress m_address;
    model::CommandStack& m_commandStack;
    engine::Transport& m_transport;
    double m_sampleRate;
    std::function<model::SnapDivision()> m_snapProvider;

    // Notes currently selected, held by value and re-matched to the clip's own notes on demand,
    // since indices shift as the clip's own vector re-sorts on every startTick change
    std::vector<model::Note> m_selection;

    DragMode m_dragMode = DragMode::None;
    int m_dragNoteIndex = -1;
    int64_t m_dragTickOffset = 0;
    // The note's value at mouseDown, before any live drag mutation, the command's "before"
    // for a single note Resize or Velocity drag, and the Move drag's delta reference note
    model::Note m_dragOriginalNote {};
    // Move only: the whole selection's values at mouseDown (the command's "before") and their
    // live current values as the drag progresses (updated every frame, never the command's after
    // until mouseUp, since the drag itself may end short of the pointer's final position)
    std::vector<model::Note> m_dragOriginalSelection;
    std::vector<model::Note> m_dragCurrentNotes;

    juce::Point<int> m_mouseDownPosition;
    bool m_hasDraggedBeyondThreshold = false;

    // Ctrl+drag on empty grid space
    // Paint stroke state, the last cell a note was laid into so a drag does not stack them
    bool m_painting = false;
    int64_t m_paintLastTick = -1;
    int m_paintLastKey = -1;

    bool m_marqueeActive = false;
    bool m_marqueeAdditive = false;
    juce::Point<int> m_marqueeStart;
    juce::Point<int> m_marqueeCurrent;

    // Vertical scroll of the key grid in pixels, centered on first layout, then wheel-driven
    int m_verticalScroll = 0;
    bool m_scrollCentered = false;

    // Horizontal zoom, 1 fits the whole clip to the grid width, higher zooms into the notes
    float m_horizontalZoom = 1.0f;

    // Horizontal scroll of the grid in pixels, only meaningful once zoomed past a fit
    int m_horizontalScroll = 0;

    // Vertical zoom multiplier over the fit height, raises the key row height
    float m_verticalZoom = 1.0f;

    // The velocity lane height, dragged by the divider at its top edge
    int m_velocityLaneHeight = kDefaultVelocityLaneHeight;

    // True while the velocity lane divider is being dragged
    bool m_draggingVelocityDivider = false;
};

} // namespace howl::ui
