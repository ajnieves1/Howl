// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per channel control surface plus a step sequencer row per MIDI track

#include "ui/ChannelRackPanel.h"

#include "ui/BrowserFileTypes.h"
#include "ui/Theme.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace howl::ui {

namespace {

// The fixed palette the recolor menu offers, packed 0xAARRGGBB
constexpr std::array<uint32_t, 12> kChannelPalette = {
    0xFFE53935, 0xFFFB8C00, 0xFFFDD835, 0xFF43A047,
    0xFF00ACC1, 0xFF1E88E5, 0xFF5E35B1, 0xFFD81B60,
    0xFF6D4C41, 0xFF757575, 0xFF00897B, 0xFFC0CA33
};

} // namespace

// Stores model references, builds the pattern combo and rows
ChannelRackPanel::ChannelRackPanel(model::Arrangement& arrangement, model::Session& session,
                                    model::PatternBank& patterns, model::CommandStack& commandStack,
                                    model::Mixer& mixer)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_patterns(patterns)
    , m_commandStack(commandStack)
    , m_mixer(mixer)
{
    addAndMakeVisible(m_patternCombo);
    addAndMakeVisible(m_addButton);
    addAndMakeVisible(m_renameButton);
    addAndMakeVisible(m_addChannelButton);

    m_patternCombo.onChange = [this] {
        updateRowControls();
        repaint();
    };
    m_addButton.onClick = [this] {
        addPattern();
    };
    m_renameButton.onClick = [this] {
        showRenameDialog();
    };
    m_addChannelButton.onClick = [this] {
        addChannel();
    };

    m_graphButton.setClickingTogglesState(true);
    m_graphButton.onClick = [this] {
        toggleGraph();
    };
    addAndMakeVisible(m_graphButton);

    m_graphParamCombo.addItem("Velocity", 1);
    m_graphParamCombo.addItem("Pitch", 2);
    m_graphParamCombo.setSelectedId(1, juce::dontSendNotification);
    m_graphParamCombo.onChange = [this] {
        repaint();
    };
    addChildComponent(m_graphParamCombo);

    refreshFromModel();
}

// Rebuilds rows and the combo after any outside model change
void ChannelRackPanel::refreshFromModel() {
    ensurePatternExists();
    rebuildFromModel();
    updateRowControls();
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

// Recomputes the MIDI track list, rebuilds rows when it changed, and refreshes the combo
void ChannelRackPanel::rebuildFromModel() {
    std::vector<std::size_t> newIndices;
    for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
        if (m_arrangement.track(i).kind == model::TrackKind::Midi) {
            newIndices.push_back(i);
        }
    }

    if (newIndices != m_midiTrackIndices) {
        m_midiTrackIndices = std::move(newIndices);
        rebuildRows();
        resized();
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

// Destroys and recreates one Row of child controls per MIDI track, wiring the mixer strip
void ChannelRackPanel::rebuildRows() {
    m_rows.clear();
    m_rows.reserve(m_midiTrackIndices.size());

    for (const std::size_t trackIndex : m_midiTrackIndices) {
        Row row;
        row.trackIndex = trackIndex;

        row.muteButton = std::make_unique<juce::TextButton>("M");
        row.muteButton->setClickingTogglesState(true);
        row.muteButton->setTooltip("Mute");
        row.muteButton->onClick = [this, trackIndex, button = row.muteButton.get()] {
            m_mixer.trackStrip(trackIndex).setMuted(button->getToggleState());
        };
        addAndMakeVisible(*row.muteButton);

        row.soloButton = std::make_unique<juce::TextButton>("S");
        row.soloButton->setClickingTogglesState(true);
        row.soloButton->setTooltip("Solo");
        row.soloButton->onClick = [this, trackIndex, button = row.soloButton.get()] {
            m_mixer.trackStrip(trackIndex).setSoloed(button->getToggleState());
        };
        addAndMakeVisible(*row.soloButton);

        row.panKnob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
            juce::Slider::NoTextBox);
        row.panKnob->setRange(-1.0, 1.0, 0.0);
        row.panKnob->setDoubleClickReturnValue(true, 0.0);
        row.panKnob->setTooltip("Pan");
        row.panKnob->onValueChange = [this, trackIndex, knob = row.panKnob.get()] {
            m_mixer.trackStrip(trackIndex).setPan(static_cast<float>(knob->getValue()));
        };
        addAndMakeVisible(*row.panKnob);

        row.volKnob = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
            juce::Slider::NoTextBox);
        row.volKnob->setRange(-60.0, 6.0, 0.0);
        row.volKnob->setDoubleClickReturnValue(true, 0.0);
        row.volKnob->setTooltip("Volume in decibels");
        row.volKnob->onValueChange = [this, trackIndex, knob = row.volKnob.get()] {
            m_mixer.trackStrip(trackIndex).setGainDb(static_cast<float>(knob->getValue()));
        };
        addAndMakeVisible(*row.volKnob);

        row.nameButton = std::make_unique<juce::TextButton>();
        row.nameButton->setTooltip("Open the instrument");
        row.nameButton->onClick = [this, trackIndex] {
            if (onInstrumentPickRequested) {
                onInstrumentPickRequested(trackIndex);
            }
        };
        addAndMakeVisible(*row.nameButton);

        row.routeButton = std::make_unique<juce::TextButton>();
        row.routeButton->setTooltip("Mixer routing");
        row.routeButton->onClick = [this, trackIndex] {
            juce::PopupMenu menu;
            const std::size_t current = m_mixer.trackOutput(trackIndex);
            menu.addItem(1, "Master", true, current == model::Mixer::kMaster);
            for (std::size_t bus = 0; bus < m_mixer.numBuses(); ++bus) {
                menu.addItem(static_cast<int>(bus + 2), juce::String(m_mixer.busName(bus)), true, current == bus);
            }
            menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex](int result) {
                if (result <= 0) {
                    return;
                }
                if (result == 1) {
                    m_mixer.setTrackOutput(trackIndex, model::Mixer::kMaster);
                } else {
                    m_mixer.setTrackOutput(trackIndex, static_cast<std::size_t>(result - 2));
                }
                updateRowControls();
            });
        };
        addAndMakeVisible(*row.routeButton);

        m_rows.push_back(std::move(row));
    }
}

// Syncs every row's control values and labels from the model without rebuilding
void ChannelRackPanel::updateRowControls() {
    for (Row& row : m_rows) {
        model::ChannelStrip& strip = m_mixer.trackStrip(row.trackIndex);
        row.muteButton->setToggleState(strip.muted(), juce::dontSendNotification);
        row.soloButton->setToggleState(strip.soloed(), juce::dontSendNotification);
        row.panKnob->setValue(strip.pan(), juce::dontSendNotification);
        row.volKnob->setValue(strip.gainDb(), juce::dontSendNotification);

        juce::String name = instrumentNameFor ? instrumentNameFor(row.trackIndex) : juce::String();
        if (name.isEmpty()) {
            name = juce::String(m_arrangement.track(row.trackIndex).name);
        }
        row.nameButton->setButtonText(name);
        row.routeButton->setButtonText(routeLabelFor(row.trackIndex));

        const juce::Colour channelColour(m_arrangement.track(row.trackIndex).color);
        row.nameButton->setColour(juce::TextButton::buttonColourId, channelColour);
        row.nameButton->setColour(juce::TextButton::textColourOffId, channelColour.contrasting());
    }
}

// Returns the display label for a track's current output destination
juce::String ChannelRackPanel::routeLabelFor(std::size_t trackIndex) const {
    const std::size_t destination = m_mixer.trackOutput(trackIndex);
    if (destination == model::Mixer::kMaster || destination >= m_mixer.numBuses()) {
        return "Master";
    }
    return juce::String(m_mixer.busName(destination));
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
    if (x < kControlsWidth) {
        return -1;
    }

    const int step = (x - kControlsWidth) / kStepSize;
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
    updateRowControls();
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

// Adds a MIDI channel and opens the instrument picker on it
void ChannelRackPanel::addChannel() {
    const juce::String name = "Channel " + juce::String(m_arrangement.numTracks() + 1);
    m_commandStack.perform(std::make_unique<model::AddTrackCommand>(
        m_arrangement, m_mixer, m_session, m_patterns, name.toStdString(), model::TrackKind::Midi));

    const std::size_t newTrackIndex = m_arrangement.numTracks() - 1;

    refreshFromModel();
    if (onTracksChanged) {
        onTracksChanged();
    }
    if (onInstrumentPickRequested) {
        onInstrumentPickRequested(newTrackIndex);
    }
}

// Removes the channel's track through the command stack
void ChannelRackPanel::deleteChannel(std::size_t trackIndex) {
    m_commandStack.perform(std::make_unique<model::RemoveTrackCommand>(
        m_arrangement, m_mixer, m_session, m_patterns, trackIndex));

    // A remove shifts the indices of later tracks, so drop the arm rather than leave it stale
    m_armedTrack = -1;
    if (onTrackSelected) {
        onTrackSelected(-1);
    }

    refreshFromModel();
    if (onTracksChanged) {
        onTracksChanged();
    }
}

// Adds a channel that copies the source track's steps, pattern notes, and instrument
void ChannelRackPanel::cloneChannel(std::size_t trackIndex) {
    const juce::String name = juce::String(m_arrangement.track(trackIndex).name) + " copy";
    m_commandStack.perform(std::make_unique<model::AddTrackCommand>(
        m_arrangement, m_mixer, m_session, m_patterns, name.toStdString(), model::TrackKind::Midi));

    const std::size_t newTrackIndex = m_arrangement.numTracks() - 1;

    // Copy every pattern's lane notes from the source channel to the clone, not undoable
    for (std::size_t p = 0; p < m_patterns.numPatterns(); ++p) {
        model::Pattern& pattern = m_patterns.pattern(p);
        if (trackIndex < pattern.trackClips.size() && newTrackIndex < pattern.trackClips.size()) {
            pattern.trackClips[newTrackIndex] = pattern.trackClips[trackIndex];
        }
    }

    refreshFromModel();
    if (onTracksChanged) {
        onTracksChanged();
    }
    // Deep plugin state is out of scope, the app copies the instrument kind and sample only
    if (onCloneInstrumentRequested) {
        onCloneInstrumentRequested(trackIndex, newTrackIndex);
    }
}

// Opens a row's right click menu: piano roll, sample, recolor, rename, clone, delete
void ChannelRackPanel::showRowMenu(std::size_t trackIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, "Edit in Piano Roll");
    menu.addItem(2, "Assign Sample...");
    menu.addItem(5, "Recolor...");
    menu.addSeparator();
    menu.addItem(3, "Clone Channel");
    menu.addItem(4, "Delete Channel");

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
        } else if (result == 5) {
            recolorChannel(trackIndex);
        } else if (result == 3) {
            cloneChannel(trackIndex);
        } else if (result == 4) {
            deleteChannel(trackIndex);
        }
    });
}

// Opens a palette menu and applies the picked color to the channel's track
void ChannelRackPanel::recolorChannel(std::size_t trackIndex) {
    if (trackIndex >= m_arrangement.numTracks()) {
        return;
    }

    juce::PopupMenu menu;
    for (std::size_t i = 0; i < kChannelPalette.size(); ++i) {
        menu.addColouredItem(static_cast<int>(i + 1), "     ", juce::Colour(kChannelPalette[i]));
    }

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex](int result) {
        if (result <= 0 || result > static_cast<int>(kChannelPalette.size())) {
            return;
        }
        if (trackIndex >= m_arrangement.numTracks()) {
            return;
        }
        m_arrangement.track(trackIndex).color = kChannelPalette[static_cast<std::size_t>(result - 1)];
        updateRowControls();
        repaint();
        if (onViewsNeedRefresh) {
            onViewsNeedRefresh();
        }
    });
}

// Arms the row's track for live input and repaints the armed highlight
void ChannelRackPanel::armTrack(std::ptrdiff_t trackIndex) {
    m_armedTrack = trackIndex;
    if (onTrackSelected) {
        onTrackSelected(trackIndex);
    }
    repaint();
}

// Lays out the top bar controls and every row's child controls
void ChannelRackPanel::resized() {
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(kTopBarHeight);
    m_patternCombo.setBounds(topBar.removeFromLeft(150).reduced(2));
    m_addButton.setBounds(topBar.removeFromLeft(30).reduced(2));
    m_renameButton.setBounds(topBar.removeFromLeft(80).reduced(2));
    m_addChannelButton.setBounds(topBar.removeFromRight(110).reduced(2));
    m_graphButton.setBounds(topBar.removeFromRight(70).reduced(2));

    const int graphTop = graphLaneTop();
    if (m_graphVisible) {
        m_graphParamCombo.setBounds(4, graphTop + 3, kControlsWidth - 8, kGraphHeaderHeight - 4);
    }

    for (std::size_t rowIndex = 0; rowIndex < m_rows.size(); ++rowIndex) {
        Row& row = m_rows[rowIndex];
        const int y = kTopBarHeight + static_cast<int>(rowIndex) * kRowHeight;

        // A row whose foot falls under the graph lane hides, so its knobs never poke through
        const bool rowFits = (y + kRowHeight) <= graphTop;
        row.muteButton->setVisible(rowFits);
        row.soloButton->setVisible(rowFits);
        row.panKnob->setVisible(rowFits);
        row.volKnob->setVisible(rowFits);
        row.nameButton->setVisible(rowFits);
        row.routeButton->setVisible(rowFits);
        if (!rowFits) {
            continue;
        }

        const int controlY = y + 3;
        const int controlHeight = kRowHeight - 6;

        int x = kSelectorWidth + 3;
        row.muteButton->setBounds(x, controlY, 22, controlHeight);
        x += 24;
        row.soloButton->setBounds(x, controlY, 22, controlHeight);
        x += 26;
        row.panKnob->setBounds(x, controlY, 32, controlHeight);
        x += 34;
        row.volKnob->setBounds(x, controlY, 32, controlHeight);
        x += 36;

        constexpr int routeWidth = 52;
        const int routeX = kControlsWidth - routeWidth - 3;
        row.nameButton->setBounds(x, controlY, routeX - x - 3, controlHeight);
        row.routeButton->setBounds(routeX, controlY, routeWidth, controlHeight);
    }
}

// Draws the top bar separator, row backgrounds, the armed highlight, and the step cells
void ChannelRackPanel::paint(juce::Graphics& g) {
    g.fillAll(theme::kWindowBg);
    g.setColour(theme::kBorder);
    g.drawHorizontalLine(kTopBarHeight - 1, 0.0f, static_cast<float>(getWidth()));

    if (m_midiTrackIndices.empty()) {
        g.setColour(theme::kTextSecondary);
        g.drawText("Add a channel to sequence steps",
            juce::Rectangle<int> { 0, kTopBarHeight, getWidth(), getHeight() - kTopBarHeight },
            juce::Justification::centred);
        return;
    }

    const std::size_t patternIndex = currentPatternIndex();
    const bool hasPattern = patternIndex < m_patterns.numPatterns();
    const model::Pattern* pattern = hasPattern ? &m_patterns.pattern(patternIndex) : nullptr;

    const int graphTop = graphLaneTop();
    g.saveState();
    g.reduceClipRegion(0, kTopBarHeight, getWidth(), graphTop - kTopBarHeight);

    for (std::size_t row = 0; row < m_midiTrackIndices.size(); ++row) {
        const std::size_t trackIndex = m_midiTrackIndices[row];
        const int y = kTopBarHeight + static_cast<int>(row) * kRowHeight;
        const bool armed = static_cast<std::ptrdiff_t>(trackIndex) == m_armedTrack;

        const juce::Colour channelColour(m_arrangement.track(trackIndex).color);

        if (armed) {
            g.setColour(theme::kSelection.withAlpha(0.12f));
            g.fillRect(0, y, getWidth(), kRowHeight);
        }

        // The arm strip on the far left doubles as the channel color chip, click it to arm
        g.setColour(channelColour);
        g.fillRect(1, y + 2, kSelectorWidth - 3, kRowHeight - 4);
        if (armed) {
            g.setColour(theme::kAccent);
            g.drawRect(1, y + 2, kSelectorWidth - 3, kRowHeight - 4, 1);
        }

        const model::MidiClip* clip = (pattern != nullptr && trackIndex < pattern->trackClips.size())
            ? &pattern->trackClips[trackIndex]
            : nullptr;

        const int cellY = y + (kRowHeight - kStepSize) / 2;
        for (int step = 0; step < kNumSteps; ++step) {
            const int x = kControlsWidth + step * kStepSize;
            const bool onBeat = (step % 4) == 0;

            g.setColour(onBeat ? theme::kRaisedBg.brighter(0.15f) : theme::kRaisedBg.darker(0.3f));
            g.fillRect(x, cellY, kStepSize, kStepSize);

            if (clip != nullptr && stepFilled(*clip, step)) {
                g.setColour(channelColour);
                g.fillRect(x + 2, cellY + 2, kStepSize - 4, kStepSize - 4);
            }

            if (m_hoverRow == static_cast<int>(row) && m_hoverStep == step) {
                g.setColour(theme::kHoverBg.withAlpha(0.6f));
                g.fillRect(x, cellY, kStepSize, kStepSize);
            }

            g.setColour(theme::kBorder);
            g.drawRect(x, cellY, kStepSize, kStepSize);
        }

        g.setColour(theme::kBorder.withAlpha(0.4f));
        g.drawHorizontalLine(y + kRowHeight - 1, 0.0f, static_cast<float>(getWidth()));
    }

    g.restoreState();

    if (m_graphVisible) {
        paintGraphLane(g);
    }
}

// Shows or hides the graph lane and relays out
void ChannelRackPanel::toggleGraph() {
    m_graphVisible = m_graphButton.getToggleState();
    m_graphParamCombo.setVisible(m_graphVisible);
    resized();
    repaint();
}

// The y where the graph lane starts, the component bottom when the lane is hidden
int ChannelRackPanel::graphLaneTop() const {
    return m_graphVisible ? getHeight() - kGraphLaneHeight : getHeight();
}

// The parameter the graph lane currently edits
ChannelRackPanel::GraphParam ChannelRackPanel::graphParam() const {
    return m_graphParamCombo.getSelectedId() == 2 ? GraphParam::Pitch : GraphParam::Velocity;
}

// Returns the index of the first note in the step's window, or -1 when the step is empty
int ChannelRackPanel::stepNoteIndex(const model::MidiClip& clip, int stepIndex) const {
    const auto& notes = clip.notes();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (noteOverlapsStep(notes[i], stepIndex)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Draws the graph lane, one value bar per filled step of the armed channel
void ChannelRackPanel::paintGraphLane(juce::Graphics& g) {
    const int top = graphLaneTop();
    g.setColour(theme::kPanelBg);
    g.fillRect(0, top, getWidth(), kGraphLaneHeight);
    g.setColour(theme::kBorder);
    g.drawHorizontalLine(top, 0.0f, static_cast<float>(getWidth()));

    const int contentTop = top + kGraphHeaderHeight;
    const int contentHeight = kGraphLaneHeight - kGraphHeaderHeight;

    const bool armedValid = m_armedTrack >= 0
        && static_cast<std::size_t>(m_armedTrack) < m_arrangement.numTracks();
    if (!armedValid) {
        g.setColour(theme::kTextSecondary);
        g.drawText("Arm a channel to edit its steps",
            juce::Rectangle<int> { kControlsWidth, contentTop, getWidth() - kControlsWidth, contentHeight },
            juce::Justification::centred);
        return;
    }

    const std::size_t armed = static_cast<std::size_t>(m_armedTrack);
    const std::size_t patternIndex = currentPatternIndex();
    const model::MidiClip* clip = (patternIndex < m_patterns.numPatterns()
        && armed < m_patterns.pattern(patternIndex).trackClips.size())
        ? &m_patterns.pattern(patternIndex).trackClips[armed]
        : nullptr;

    g.setColour(theme::kBorder.withAlpha(0.3f));
    for (int step = 0; step <= kNumSteps; ++step) {
        const int x = kControlsWidth + step * kStepSize;
        g.drawVerticalLine(x, static_cast<float>(contentTop), static_cast<float>(top + kGraphLaneHeight));
    }

    if (clip == nullptr) {
        return;
    }

    const juce::Colour channelColour(m_arrangement.track(armed).color);
    for (int step = 0; step < kNumSteps; ++step) {
        const int noteIndex = stepNoteIndex(*clip, step);
        if (noteIndex < 0) {
            continue;
        }

        const model::Note& note = clip->notes()[static_cast<std::size_t>(noteIndex)];
        const float value = graphParam() == GraphParam::Pitch
            ? juce::jlimit(0.0f, 1.0f,
                static_cast<float>(note.key - kMinGraphKey) / static_cast<float>(kMaxGraphKey - kMinGraphKey))
            : note.velocity;

        const int barHeight = static_cast<int>(value * static_cast<float>(contentHeight));
        const int x = kControlsWidth + step * kStepSize;
        g.setColour(channelColour);
        g.fillRect(x + 3, contentTop + contentHeight - barHeight, kStepSize - 6, barHeight);
    }
}

// Left click toggles the step under the cursor, right click opens the row menu
void ChannelRackPanel::mouseDown(const juce::MouseEvent& event) {
    // A click inside the graph lane's content edits the armed channel's step values
    if (m_graphVisible && event.y >= graphLaneTop() + kGraphHeaderHeight) {
        const int step = stepAtX(event.x);
        const bool armedValid = m_armedTrack >= 0
            && static_cast<std::size_t>(m_armedTrack) < m_arrangement.numTracks();
        if (step < 0 || !armedValid) {
            return;
        }

        const model::ClipAddress address { model::ClipAddress::Source::Pattern,
            static_cast<std::size_t>(m_armedTrack), currentPatternIndex() };
        model::MidiClip* clip = model::resolveClip(m_arrangement, m_session, &m_patterns, address);
        if (clip == nullptr) {
            return;
        }

        const int noteIndex = stepNoteIndex(*clip, step);
        if (noteIndex < 0) {
            return;
        }

        m_draggingGraph = true;
        m_graphDragStep = step;
        m_graphDragNoteIndex = noteIndex;
        m_graphDragBefore = clip->notes()[static_cast<std::size_t>(noteIndex)];
        dragGraphValue(event.y);
        return;
    }

    const int row = rowAtY(event.y);
    if (row < 0) {
        return;
    }

    const std::size_t trackIndex = m_midiTrackIndices[static_cast<std::size_t>(row)];

    if (event.x < kSelectorWidth) {
        armTrack(m_armedTrack == static_cast<std::ptrdiff_t>(trackIndex)
            ? static_cast<std::ptrdiff_t>(-1)
            : static_cast<std::ptrdiff_t>(trackIndex));
        return;
    }

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

// Mutates the dragged step's note from the graph cursor y, live, no command until mouseUp
void ChannelRackPanel::dragGraphValue(int y) {
    if (!m_draggingGraph || m_graphDragNoteIndex < 0) {
        return;
    }

    const model::ClipAddress address { model::ClipAddress::Source::Pattern,
        static_cast<std::size_t>(m_armedTrack), currentPatternIndex() };
    model::MidiClip* clip = model::resolveClip(m_arrangement, m_session, &m_patterns, address);
    if (clip == nullptr || static_cast<std::size_t>(m_graphDragNoteIndex) >= clip->notes().size()) {
        return;
    }

    const int contentTop = graphLaneTop() + kGraphHeaderHeight;
    const int contentHeight = kGraphLaneHeight - kGraphHeaderHeight;
    const float ratio = juce::jlimit(0.0f, 1.0f,
        1.0f - static_cast<float>(y - contentTop) / static_cast<float>(contentHeight));

    model::Note note = clip->notes()[static_cast<std::size_t>(m_graphDragNoteIndex)];
    if (graphParam() == GraphParam::Pitch) {
        note.key = kMinGraphKey + static_cast<int>(std::lround(ratio * static_cast<float>(kMaxGraphKey - kMinGraphKey)));
    } else {
        note.velocity = juce::jlimit(0.05f, 1.0f, ratio);
    }

    m_graphDragNoteIndex = static_cast<int>(
        clip->replaceNoteAt(static_cast<std::size_t>(m_graphDragNoteIndex), note));
    repaint();
}

// Drags a graph value while a graph edit is active
void ChannelRackPanel::mouseDrag(const juce::MouseEvent& event) {
    if (m_draggingGraph) {
        dragGraphValue(event.y);
    }
}

// Commits a graph drag as one undoable command, the live mutation is already the after state
void ChannelRackPanel::mouseUp(const juce::MouseEvent&) {
    if (!m_draggingGraph) {
        return;
    }
    m_draggingGraph = false;

    const bool armedValid = m_armedTrack >= 0
        && static_cast<std::size_t>(m_armedTrack) < m_arrangement.numTracks();
    if (m_graphDragNoteIndex >= 0 && armedValid) {
        const model::ClipAddress address { model::ClipAddress::Source::Pattern,
            static_cast<std::size_t>(m_armedTrack), currentPatternIndex() };
        model::MidiClip* clip = model::resolveClip(m_arrangement, m_session, &m_patterns, address);
        if (clip != nullptr && static_cast<std::size_t>(m_graphDragNoteIndex) < clip->notes().size()) {
            const model::Note after = clip->notes()[static_cast<std::size_t>(m_graphDragNoteIndex)];
            m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(
                m_arrangement, m_session, &m_patterns, address,
                std::vector<model::Note> { m_graphDragBefore }, std::vector<model::Note> { after }));
        }
    }

    m_graphDragNoteIndex = -1;
    m_graphDragStep = -1;
    repaint();
}

// Tracks which step cell the cursor is over, for the hover highlight
void ChannelRackPanel::mouseMove(const juce::MouseEvent& event) {
    const int row = rowAtY(event.y);
    const int step = row < 0 ? -1 : stepAtX(event.x);

    if (row != m_hoverRow || step != m_hoverStep) {
        m_hoverRow = row;
        m_hoverStep = step;
        repaint();
    }
}

// Clears the hover highlight once the cursor leaves the panel
void ChannelRackPanel::mouseExit(const juce::MouseEvent&) {
    if (m_hoverRow != -1 || m_hoverStep != -1) {
        m_hoverRow = -1;
        m_hoverStep = -1;
        repaint();
    }
}

// Accepts a drag whose description matches the browser's "howl-file" tag
bool ChannelRackPanel::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    return dragSourceDetails.description == juce::var("howl-file");
}

// Assigns the dropped file to the row under the drop point when it is an audio sample
void ChannelRackPanel::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) {
    const int row = rowAtY(dragSourceDetails.localPosition.y);
    if (row < 0 || browserFileProvider == nullptr) {
        return;
    }

    const juce::File file = browserFileProvider();
    const std::size_t trackIndex = m_midiTrackIndices[static_cast<std::size_t>(row)];

    // An audio file installs a sampler, a preset file loads into the channel's plugin
    if (filetypes::isAudioFile(file)) {
        if (onSampleAssignRequested) {
            onSampleAssignRequested(trackIndex, file);
        }
    } else if (filetypes::isPatchFile(file)) {
        if (onPatchDropRequested) {
            onPatchDropRequested(trackIndex, file);
        }
    }
}

} // namespace howl::ui
