// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: pattern selector plus one step sequencer row per MIDI track

#pragma once

#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/Commands.h"
#include "model/Pattern.h"
#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace howl::ui {

// Pattern selector plus one step-sequencer row per MIDI track
class ChannelRackPanel : public juce::Component, public juce::DragAndDropTarget {
public:
    // Stores model references, builds the pattern combo and rows
    ChannelRackPanel(model::Arrangement& arrangement, model::Session& session, model::PatternBank& patterns,
                      model::CommandStack& commandStack);

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

    // Provides the file behind a "howl-sample" drag, wired to the browser
    std::function<juce::File()> browserFileProvider;

    // Nothing to lay out below the top bar, every row is custom painted
    void resized() override;

    // Draws the pattern top bar's separator and every row's label plus 16 step cells
    void paint(juce::Graphics& g) override;

    // Left click toggles the step under the cursor, right click opens the row menu
    void mouseDown(const juce::MouseEvent& event) override;

    // Accepts a drag whose description matches the browser's "howl-sample" tag
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

    // Assigns the dropped sample to the row under the drop point
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

private:
    static constexpr int kTopBarHeight = 28;
    static constexpr int kRowHeight = 24;
    static constexpr int kTrackLabelWidth = 150;
    static constexpr int kNumSteps = 16;
    static constexpr int64_t kStepTicks = model::kTicksPerQuarter / 4; // one 16th
    static constexpr int64_t kMinLaneLengthTicks = model::kTicksPerQuarter * 4; // one bar

    // Ensures at least one pattern exists, lazily creating "Pattern 1" sized to the track count
    void ensurePatternExists();

    // Rebuilds the MIDI track row list and the pattern combo's items from the model
    void rebuildFromModel();

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

    // Opens a row's right click menu: Edit in Piano Roll, Assign Sample...
    void showRowMenu(std::size_t trackIndex);

    model::Arrangement& m_arrangement;
    model::Session& m_session;
    model::PatternBank& m_patterns;
    model::CommandStack& m_commandStack;

    juce::ComboBox m_patternCombo;
    juce::TextButton m_addButton { "+" };
    juce::TextButton m_renameButton { "Rename..." };

    // Row index -> arrangement track index, MIDI kind tracks only
    std::vector<std::size_t> m_midiTrackIndices;
};

} // namespace howl::ui
