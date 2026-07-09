// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows tracks as lanes and clips as blocks, edited through the command stack

#pragma once

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/SnapGrid.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace howl::ui {

// v1 scope: drag a clip's body to move it, issuing one move-clip command per
// drag gesture, not one per mouse-move. Editing is only tested with the
// transport stopped, same caveat as PianoRoll. Clicking a MIDI clip without
// dragging fires onMidiClipSelected, actually opening a PianoRoll for it is
// wired in P4-T7
class ArrangeView : public juce::Component, public juce::FileDragAndDropTarget,
                    public juce::DragAndDropTarget, private juce::Timer {
public:
    // Stores references to the arrangement, transport, and command stack, starts the playhead timer
    ArrangeView(model::Arrangement& arrangement, engine::Transport& transport,
                model::CommandStack& commandStack, double sampleRate,
                std::function<model::SnapDivision()> snapProvider);

    // Stops the playhead timer
    ~ArrangeView() override;

    // Draws the ruler, one lane per track, each clip as a block, and the playhead
    void paint(juce::Graphics& g) override;

    // Begins a move or resize drag if the click landed on a clip, opens a delete menu on right-click
    void mouseDown(const juce::MouseEvent& event) override;

    // Continues a move or resize drag once the mouse has moved past a small threshold
    void mouseDrag(const juce::MouseEvent& event) override;

    // Shows a left-right resize cursor when hovering a MIDI clip's right edge
    void mouseMove(const juce::MouseEvent& event) override;

    // Issues the move or resize clip command for a completed drag, or fires onMidiClipSelected
    // for a plain click
    void mouseUp(const juce::MouseEvent& event) override;

    // Creates a new 4-bar MIDI clip on an empty MIDI lane, snapped to the bar
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // Ctrl+wheel zooms around the cursor, plain wheel scrolls horizontally
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // Toggles the transport between play and stop on space, fires onMixerRequested on M,
    // rewinds to 0 on Home
    bool keyPressed(const juce::KeyPress& key) override;

    // Accepts a drag hovering any .wav file
    bool isInterestedInFileDrag(const juce::StringArray& files) override;

    // Fires onAudioFileDropped with the drop lane and snapped tick for each dropped .wav
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Accepts a drag whose description matches the browser's "howl-sample" tag
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

    // Fires onAudioFileDropped for the browser's currently selected file, same lane/tick math as filesDropped
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

    // Called with (trackIndex, placementIndex) when a MIDI clip is clicked without dragging
    std::function<void(std::size_t, std::size_t)> onMidiClipSelected;

    // Called when M is pressed to open the mixer
    std::function<void()> onMixerRequested;

    // Called with (path, trackIndex, tick) when a .wav file is dropped onto a lane
    std::function<void(juce::String, std::size_t, int64_t)> onAudioFileDropped;

    // Called after an audio clip's warp toggle or original BPM changes
    std::function<void()> onWarpChanged;

    // Provides the file behind a "howl-sample" drag, wired to the browser's selection
    std::function<juce::File()> browserFileProvider;

private:
    static constexpr int64_t kMinimumVisibleTicks = model::kTicksPerQuarter * 16; // 4 bars at 4/4
    static constexpr int kDragThresholdPixels = 4;
    static constexpr int kResizeHandlePixels = 6;
    static constexpr int kRulerHeight = 20;
    static constexpr double kMinZoom = 1.0;
    static constexpr double kMaxZoom = 64.0;

    enum class ClipKind {
        Midi,
        Audio
    };

    enum class ClipDragMode {
        None,
        Move,
        Resize,
        Duplicate
    };

    struct DraggedClip {
        ClipKind kind;
        std::size_t trackIndex;
        std::size_t placementIndex;
        int64_t originalStartTick;
    };

    // A selected clip, held by (kind, track, placement index). A move re-sorts placements by
    // tick same as note commands re-sort by tick, so the index alone is not stable across a
    // move; the selection is rebuilt afterward from each command's own resulting index
    struct ClipRef {
        ClipKind kind;
        std::size_t trackIndex;
        std::size_t placementIndex;
    };

    // Repaints so the playhead position stays current during playback
    void timerCallback() override;

    // Samples per tick from the transport's current tempo and this view's sample rate
    double samplesPerTick() const;

    // Ticks spanned by the visible timeline, covers every placed clip plus a sensible minimum
    int64_t visibleTickSpan() const;

    // Ticks spanned by the current zoom level, visibleTickSpan() / m_zoom
    double zoomedVisibleSpan() const;

    // Clamps m_scrollTick to [0, visibleTickSpan() - zoomedVisibleSpan()]
    void clampScroll();

    // Converts a pixel x position to a tick, offset by the current scroll
    int64_t xToTick(int x) const;

    // Converts a tick to a pixel x position, offset by the current scroll
    float tickToX(int64_t tick) const;

    // Returns the height of one track lane, below the ruler
    float laneHeight() const;

    // Converts a pixel y position to a track index, clamped to numTracks() - 1
    std::size_t yToTrackIndex(int y) const;

    // Returns the current transport position converted to ticks
    double playheadTick() const;

    // Finds the clip under (trackIndex, tick), fills found and returns true on a hit
    bool hitTestClip(std::size_t trackIndex, int64_t tick, DraggedClip& found) const;

    // True when x sits within kResizeHandlePixels of clip's right edge, always false for audio
    bool isNearResizeHandle(const DraggedClip& clip, int x) const;

    // The snap unit resize clamps to, 240 (a step) rather than a beat when the division is Off,
    // per this task's own contract, deliberately not the same "Beat when Off" convention P9 uses
    int64_t minimumResizeLengthTicks(model::SnapDivision division) const;

    // True when (kind, trackIndex, placementIndex) matches an entry in m_selection
    bool isSelected(ClipKind kind, std::size_t trackIndex, std::size_t placementIndex) const;

    // Selects every clip (both kinds, any track) intersecting the marquee rectangle, replacing
    // the current selection; an empty sweep (a plain click that never dragged) clears it
    void finalizeClipMarquee();

    // Fills outStartTick with the group-move preview position for (kind, trackIndex,
    // placementIndex) and returns true, or returns false when it is not part of the drag
    bool findGroupPreviewTick(ClipKind kind, std::size_t trackIndex, std::size_t placementIndex,
                              int64_t& outStartTick) const;

    // Opens a "Delete Clip" popup for the given clip, with warp toggle and BPM entry for audio clips
    void showDeleteClipMenu(const DraggedClip& target);

    // Opens an async "Set Original BPM..." dialog for the given audio clip
    void showSetOriginalBpmDialog(const DraggedClip& target);

    // Opens a one-item "Loop: On/Off" menu for the ruler, toggles looping without moving the region
    void showRulerMenu();

    // Returns the current global snap division, Step when no provider is set
    model::SnapDivision snapDivision() const;

    model::Arrangement& m_arrangement;
    engine::Transport& m_transport;
    model::CommandStack& m_commandStack;
    double m_sampleRate;
    std::function<model::SnapDivision()> m_snapProvider;

    double m_zoom = 1.0;
    int64_t m_scrollTick = 0;

    ClipDragMode m_clipDragMode = ClipDragMode::None;
    DraggedClip m_draggedClip {};
    int64_t m_dragCurrentTick = 0;
    // Resize only: the length at mouseDown (the command's "before") and the live, already
    // mutated length as the drag progresses (the command's "after" once mouseUp commits it)
    int64_t m_dragOriginalLengthTicks = 0;
    int64_t m_dragCurrentLengthTicks = 0;
    juce::Point<int> m_mouseDownPosition;
    bool m_hasDraggedBeyondThreshold = false;

    // True while a mouse gesture is sweeping a loop region across the ruler
    bool m_rulerDragging = false;
    int64_t m_rulerAnchorTick = 0;
    int64_t m_rulerCurrentTick = 0;

    // The persistent multi-selection, independent of any drag in progress
    std::vector<ClipRef> m_selection;

    // True while a mouse gesture is sweeping a clip-selection marquee across empty lane space
    bool m_clipMarqueeActive = false;
    juce::Point<int> m_clipMarqueeStart;
    juce::Point<int> m_clipMarqueeCurrent;

    // Move only: the whole selection's values at mouseDown (the command's "before" set),
    // used to compute every member's live preview position from one shared tick delta
    std::vector<DraggedClip> m_dragGroupOriginal;
};

} // namespace howl::ui
