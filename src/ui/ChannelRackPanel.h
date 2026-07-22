// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per channel control surface plus a step sequencer row per MIDI track

#pragma once

#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/Commands.h"
#include "model/Mixer.h"
#include "model/Pattern.h"
#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace howl::ui {

// One control row per MIDI track: an arm selector, mute and solo, pan and volume knobs, the
// instrument name button, a mixer routing readout, and a 16 step grid, above a pattern selector
class ChannelRackPanel : public juce::Component, public juce::DragAndDropTarget {
public:
    // Stores model references, builds the pattern combo and rows
    ChannelRackPanel(model::Arrangement& arrangement, model::Session& session, model::PatternBank& patterns,
                      model::CommandStack& commandStack, model::Mixer& mixer);

    // Rebuilds rows and the combo after any outside model change
    void refreshFromModel();

    // Returns the pattern the combo has selected, for arrange view placement
    std::size_t currentPatternIndex() const;

    // Fired when a row asks to edit its lane clip in the piano roll
    std::function<void(model::ClipAddress)> onSlotEditRequested;

    // Fired when a sample lands on a row, the app installs a SamplerInstrument
    std::function<void(std::size_t, juce::File)> onSampleAssignRequested;

    // Fired after a step toggles on, the app previews the hit when stopped
    std::function<void(std::size_t)> onStepPreviewRequested;

    // Fired when a channel name button is clicked, the app opens the instrument picker
    std::function<void(std::size_t)> onInstrumentPickRequested;

    // Fired when a row's Open Editor item is picked, the app opens the plugin editor
    std::function<void(std::size_t)> onInstrumentEditRequested;

    // Queried for a track's current instrument display name, shown on the name button
    std::function<juce::String(std::size_t)> instrumentNameFor;

    // Fired when a row is armed for live input, or -1 when none
    std::function<void(std::ptrdiff_t)> onTrackSelected;

    // Fired after a channel is added or removed so the app rebuilds the graph and views
    std::function<void()> onTracksChanged;

    // Fired to copy the instrument from a source channel to a cloned one, deep plugin state aside
    std::function<void(std::size_t, std::size_t)> onCloneInstrumentRequested;

    // Fired when a channel color changes so the app repaints the arrange view and roll
    std::function<void()> onViewsNeedRefresh;

    // Provides the file behind a "howl-file" drag, wired to the browser
    std::function<juce::File()> browserFileProvider;

    // Lays out the top bar controls and every row's child controls
    void resized() override;

    // Draws the top bar separator, row backgrounds, the armed highlight, and the step cells
    void paint(juce::Graphics& g) override;

    // Left click toggles the step under the cursor, right click opens the row menu
    void mouseDown(const juce::MouseEvent& event) override;

    // Tracks which step cell the cursor is over, for the hover highlight
    void mouseMove(const juce::MouseEvent& event) override;

    // Clears the hover highlight once the cursor leaves the panel
    void mouseExit(const juce::MouseEvent& event) override;

    // Accepts a drag whose description matches the browser's "howl-file" tag
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

    // Assigns the dropped file to the row under the drop point when it is an audio sample
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

private:
    static constexpr int kTopBarHeight = 30;
    static constexpr int kRowHeight = 36;
    static constexpr int kControlsWidth = 300;
    static constexpr int kSelectorWidth = 8;
    static constexpr int kStepSize = 24;
    static constexpr int kNumSteps = 16;
    static constexpr int64_t kStepTicks = model::kTicksPerQuarter / 4; // one 16th
    static constexpr int64_t kMinLaneLengthTicks = model::kTicksPerQuarter * 4; // one bar

    // The child controls for one channel row, all bound to the track's mixer strip
    struct Row {
        std::size_t trackIndex = 0;
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
        std::unique_ptr<juce::Slider> panKnob;
        std::unique_ptr<juce::Slider> volKnob;
        std::unique_ptr<juce::TextButton> nameButton;
        std::unique_ptr<juce::TextButton> routeButton;
    };

    // Ensures at least one pattern exists, lazily creating "Pattern 1" sized to the track count
    void ensurePatternExists();

    // Recomputes the MIDI track list, rebuilds rows when it changed, and refreshes the combo
    void rebuildFromModel();

    // Destroys and recreates one Row of child controls per MIDI track, wiring the mixer strip
    void rebuildRows();

    // Syncs every row's control values and labels from the model without rebuilding
    void updateRowControls();

    // Returns the display label for a track's current output destination
    juce::String routeLabelFor(std::size_t trackIndex) const;

    // Returns the row index at y, or -1 when above the rows or past the last one
    int rowAtY(int y) const;

    // Returns the step index at x, or -1 when outside the 16 cell grid
    int stepAtX(int x) const;

    // True when note's span intersects step index's 240 tick window, the pinned window rule
    static bool noteOverlapsStep(const model::Note& note, int stepIndex);

    // True when any note in clip overlaps step index's window
    static bool stepFilled(const model::MidiClip& clip, int stepIndex);

    // Adds a note when the step is off, removes every overlapping note as one CompositeCommand
    // when it is on; raises the lane's lengthTicks to at least one bar before an add
    void toggleStep(std::size_t trackIndex, int stepIndex);

    // Appends "Pattern N" sized to the track count and selects it, not undoable
    void addPattern();

    // Opens an async rename dialog for the selected pattern, not undoable
    void showRenameDialog();

    // Adds a MIDI channel and opens the instrument picker on it
    void addChannel();

    // Removes the channel's track through the command stack
    void deleteChannel(std::size_t trackIndex);

    // Adds a channel that copies the source track's steps, pattern notes, and instrument
    void cloneChannel(std::size_t trackIndex);

    // Opens a palette menu and applies the picked color to the channel's track
    void recolorChannel(std::size_t trackIndex);

    // Opens a row's right click menu: piano roll, sample, recolor, rename, clone, delete
    void showRowMenu(std::size_t trackIndex);

    // Arms the row's track for live input and repaints the armed highlight
    void armTrack(std::ptrdiff_t trackIndex);

    model::Arrangement& m_arrangement;
    model::Session& m_session;
    model::PatternBank& m_patterns;
    model::CommandStack& m_commandStack;
    model::Mixer& m_mixer;

    juce::ComboBox m_patternCombo;
    juce::TextButton m_addButton { "+" };
    juce::TextButton m_renameButton { "Rename..." };
    juce::TextButton m_addChannelButton { "Add Channel" };

    // Row index -> arrangement track index, MIDI kind tracks only
    std::vector<std::size_t> m_midiTrackIndices;
    std::vector<Row> m_rows;

    // The armed track for live input, -1 when none, drives the row highlight
    std::ptrdiff_t m_armedTrack = -1;

    // The cell currently under the cursor, for the hover highlight; -1 when over neither
    int m_hoverRow = -1;
    int m_hoverStep = -1;
};

} // namespace howl::ui
