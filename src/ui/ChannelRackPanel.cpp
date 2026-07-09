// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: pattern selector plus one step sequencer row per MIDI track

#include "ui/ChannelRackPanel.h"

#include <memory>
#include <string>

namespace howl::ui {

// Stores model references, builds the pattern combo and rows
ChannelRackPanel::ChannelRackPanel(model::Arrangement& arrangement, model::Session& session,
                                    model::PatternBank& patterns, model::CommandStack& commandStack)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_patterns(patterns)
    , m_commandStack(commandStack)
{
    addAndMakeVisible(m_patternCombo);
    addAndMakeVisible(m_addButton);
    addAndMakeVisible(m_renameButton);

    m_patternCombo.onChange = [this] {
        repaint();
    };
    m_addButton.onClick = [this] {
        addPattern();
    };
    m_renameButton.onClick = [this] {
        showRenameDialog();
    };

    refreshFromModel();
}

// Rebuilds rows and the combo after any outside model change
void ChannelRackPanel::refreshFromModel() {
    ensurePatternExists();
    rebuildFromModel();
    repaint();
}

// Returns the pattern the combo has selected, for arrange view placement
std::size_t ChannelRackPanel::currentPatternIndex() const {
    const int selectedId = m_patternCombo.getSelectedId();
    return selectedId > 0 ? static_cast<std::size_t>(selectedId - 1) : 0;
}

// Ensures at least one pattern exists, lazily creating "Pattern 1" sized to the track count
void ChannelRackPanel::ensurePatternExists() {
    if (m_patterns.numPatterns() == 0) {
        m_patterns.addPattern("Pattern 1", m_arrangement.numTracks());
    }
}

// Rebuilds the MIDI track row list and the pattern combo's items from the model
void ChannelRackPanel::rebuildFromModel() {
    m_midiTrackIndices.clear();
    for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
        if (m_arrangement.track(i).kind == model::TrackKind::Midi) {
            m_midiTrackIndices.push_back(i);
        }
    }

    const int previousId = m_patternCombo.getSelectedId();
    m_patternCombo.clear(juce::dontSendNotification);
    for (std::size_t i = 0; i < m_patterns.numPatterns(); ++i) {
        m_patternCombo.addItem(juce::String(m_patterns.pattern(i).name), static_cast<int>(i + 1));
    }

    if (previousId > 0 && previousId <= static_cast<int>(m_patterns.numPatterns())) {
        m_patternCombo.setSelectedId(previousId, juce::dontSendNotification);
    } else if (m_patterns.numPatterns() > 0) {
        m_patternCombo.setSelectedId(1, juce::dontSendNotification);
    }
}

// Returns the row index at y, or -1 when above the rows or past the last one
int ChannelRackPanel::rowAtY(int y) const {
    if (y < kTopBarHeight) {
        return -1;
    }

    const int row = (y - kTopBarHeight) / kRowHeight;
    return row < static_cast<int>(m_midiTrackIndices.size()) ? row : -1;
}

// Returns the step index at x, or -1 when outside the 16 cell grid
int ChannelRackPanel::stepAtX(int x) const {
    if (x < kTrackLabelWidth) {
        return -1;
    }

    const int step = (x - kTrackLabelWidth) / kRowHeight; // square cells, reusing row height as width
    return step < kNumSteps ? step : -1;
}

// True when note's span intersects step index's 240 tick window, the pinned window rule
bool ChannelRackPanel::noteOverlapsStep(const model::Note& note, int stepIndex) {
    const int64_t windowStart = static_cast<int64_t>(stepIndex) * kStepTicks;
    const int64_t windowEnd = windowStart + kStepTicks;
    return note.startTick < windowEnd && note.startTick + note.lengthTicks > windowStart;
}

// True when any note in clip overlaps step index's window
bool ChannelRackPanel::stepFilled(const model::MidiClip& clip, int stepIndex) {
    for (const model::Note& note : clip.notes()) {
        if (noteOverlapsStep(note, stepIndex)) {
            return true;
        }
    }
    return false;
}

// Adds a note when the step is off, removes every overlapping note as one CompositeCommand
// when it is on; raises the lane's lengthTicks to at least one bar before an add
void ChannelRackPanel::toggleStep(std::size_t trackIndex, int stepIndex) {
    const model::ClipAddress address { model::ClipAddress::Source::Pattern, trackIndex, currentPatternIndex() };
    model::MidiClip* clip = model::resolveClip(m_arrangement, m_session, &m_patterns, address);
    if (clip == nullptr) {
        return;
    }

    std::vector<model::Note> notesInWindow;
    for (const model::Note& note : clip->notes()) {
        if (noteOverlapsStep(note, stepIndex)) {
            notesInWindow.push_back(note);
        }
    }

    if (!notesInWindow.empty()) {
        auto composite = std::make_unique<model::CompositeCommand>();
        for (const model::Note& note : notesInWindow) {
            composite->add(std::make_unique<model::RemoveNoteCommand>(
                m_arrangement, m_session, &m_patterns, address, note));
        }
        m_commandStack.perform(std::move(composite));
        return;
    }

    if (clip->lengthTicks() < kMinLaneLengthTicks) {
        clip->setLengthTicks(kMinLaneLengthTicks);
    }

    const int64_t windowStart = static_cast<int64_t>(stepIndex) * kStepTicks;
    const model::Note note { 60, 0.8f, windowStart, kStepTicks };
    m_commandStack.perform(std::make_unique<model::AddNoteCommand>(m_arrangement, m_session, &m_patterns, address, note));

    if (onStepPreviewRequested) {
        onStepPreviewRequested(trackIndex);
    }
}

// Appends "Pattern N" sized to the track count and selects it, not undoable
void ChannelRackPanel::addPattern() {
    const std::size_t index = m_patterns.addPattern(
        "Pattern " + std::to_string(m_patterns.numPatterns() + 1), m_arrangement.numTracks());
    rebuildFromModel();
    m_patternCombo.setSelectedId(static_cast<int>(index + 1), juce::dontSendNotification);
    repaint();
}

// Opens an async rename dialog for the selected pattern, not undoable
void ChannelRackPanel::showRenameDialog() {
    const std::size_t index = currentPatternIndex();
    if (index >= m_patterns.numPatterns()) {
        return;
    }

    auto* window = new juce::AlertWindow("Rename Pattern", "New name:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", juce::String(m_patterns.pattern(index).name));
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true, juce::ModalCallbackFunction::create([this, index, window](int result) {
        if (result == 1) {
            const juce::String newName = window->getTextEditorContents("name");
            if (newName.isNotEmpty()) {
                m_patterns.pattern(index).name = newName.toStdString();
                rebuildFromModel();
                repaint();
            }
        }
    }), true);
}

// Opens a row's right click menu: Edit in Piano Roll, Assign Sample...
void ChannelRackPanel::showRowMenu(std::size_t trackIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, "Edit in Piano Roll");
    menu.addItem(2, "Assign Sample...");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex](int result) {
        if (result == 1) {
            if (onSlotEditRequested) {
                onSlotEditRequested(model::ClipAddress {
                    model::ClipAddress::Source::Pattern, trackIndex, currentPatternIndex() });
            }
        } else if (result == 2) {
            auto chooser = std::make_shared<juce::FileChooser>("Assign Sample", juce::File(), "*.wav");
            constexpr int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

            chooser->launchAsync(chooserFlags, [this, trackIndex, chooser](const juce::FileChooser& fc) {
                const juce::File file = fc.getResult();
                if (file != juce::File() && onSampleAssignRequested) {
                    onSampleAssignRequested(trackIndex, file);
                }
            });
        }
    });
}

// Nothing to lay out below the top bar, every row is custom painted
void ChannelRackPanel::resized() {
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(kTopBarHeight);
    m_patternCombo.setBounds(topBar.removeFromLeft(150).reduced(2));
    m_addButton.setBounds(topBar.removeFromLeft(30).reduced(2));
    m_renameButton.setBounds(topBar.removeFromLeft(80).reduced(2));
}

// Draws the pattern top bar's separator and every row's label plus 16 step cells
void ChannelRackPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::grey);
    g.drawHorizontalLine(kTopBarHeight - 1, 0.0f, static_cast<float>(getWidth()));

    const std::size_t patternIndex = currentPatternIndex();
    const bool hasPattern = patternIndex < m_patterns.numPatterns();
    const model::Pattern* pattern = hasPattern ? &m_patterns.pattern(patternIndex) : nullptr;

    for (std::size_t row = 0; row < m_midiTrackIndices.size(); ++row) {
        const std::size_t trackIndex = m_midiTrackIndices[row];
        const int y = kTopBarHeight + static_cast<int>(row) * kRowHeight;

        g.setColour(juce::Colours::white);
        g.drawText(juce::String(m_arrangement.track(trackIndex).name), 4, y, kTrackLabelWidth - 8, kRowHeight,
            juce::Justification::centredLeft);

        const model::MidiClip* clip = (pattern != nullptr && trackIndex < pattern->trackClips.size())
            ? &pattern->trackClips[trackIndex]
            : nullptr;

        for (int step = 0; step < kNumSteps; ++step) {
            const int x = kTrackLabelWidth + step * kRowHeight;
            const bool onBeat = (step % 4) == 0;

            g.setColour(onBeat ? juce::Colours::darkgrey.brighter(0.15f) : juce::Colours::darkgrey.darker(0.3f));
            g.fillRect(x, y, kRowHeight, kRowHeight);

            if (clip != nullptr && stepFilled(*clip, step)) {
                g.setColour(juce::Colours::orange);
                g.fillRect(x + 2, y + 2, kRowHeight - 4, kRowHeight - 4);
            }

            g.setColour(juce::Colours::black);
            g.drawRect(x, y, kRowHeight, kRowHeight);
        }
    }
}

// Left click toggles the step under the cursor, right click opens the row menu
void ChannelRackPanel::mouseDown(const juce::MouseEvent& event) {
    const int row = rowAtY(event.y);
    if (row < 0) {
        return;
    }

    const std::size_t trackIndex = m_midiTrackIndices[static_cast<std::size_t>(row)];

    if (event.mods.isPopupMenu()) {
        showRowMenu(trackIndex);
        return;
    }

    const int step = stepAtX(event.x);
    if (step < 0) {
        return;
    }

    toggleStep(trackIndex, step);
    repaint();
}

// Accepts a drag whose description matches the browser's "howl-sample" tag
bool ChannelRackPanel::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    return dragSourceDetails.description == juce::var("howl-sample");
}

// Assigns the dropped sample to the row under the drop point
void ChannelRackPanel::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    const int row = rowAtY(dragSourceDetails.localPosition.y);
    if (row < 0) {
        return;
    }

    if (onSampleAssignRequested && browserFileProvider) {
        onSampleAssignRequested(m_midiTrackIndices[static_cast<std::size_t>(row)], browserFileProvider());
    }
}

} // namespace howl::ui
