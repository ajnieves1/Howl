// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: left column, one header row per track, aligned with the arrange view's lanes

#include "ui/TrackHeaderPanel.h"

#include "model/Commands.h"

namespace howl::ui {

// Reports right-clicks via onRightClick, otherwise behaves like a normal editable label
void TrackHeaderPanel::NameLabel::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) {
        if (onRightClick) {
            onRightClick();
        }
        return;
    }

    Label::mouseDown(event);
}

// Stores references, builds the initial rows, starts the 30 Hz mirroring timer
TrackHeaderPanel::TrackHeaderPanel(model::Arrangement& arrangement, model::Mixer& mixer,
                                    model::CommandStack& commandStack)
    : m_arrangement(arrangement)
    , m_mixer(mixer)
    , m_commandStack(commandStack)
{
    m_addTrackButton.onClick = [this] {
        showAddTrackMenu();
    };
    addAndMakeVisible(m_addTrackButton);

    refreshFromModel();
    startTimerHz(30);
}

// Stops the timer
TrackHeaderPanel::~TrackHeaderPanel() {
    stopTimer();
}

// Returns the height of one track lane, matching ArrangeView's lane math
float TrackHeaderPanel::laneHeight() const {
    const std::size_t numTracks = m_arrangement.numTracks();
    if (numTracks == 0) {
        return static_cast<float>(getHeight());
    }

    return static_cast<float>(getHeight()) / static_cast<float>(numTracks);
}

// Lays out one row per track, then the add-track button pinned at the bottom
void TrackHeaderPanel::resized() {
    const float height = laneHeight();

    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        const int y = static_cast<int>(static_cast<float>(i) * height);
        const int rowHeight = static_cast<int>(height);

        auto bounds = juce::Rectangle<int>(0, y, getWidth(), rowHeight).reduced(2);

        row.nameLabel->setBounds(bounds.removeFromTop(16));

        if (row.instrumentButton != nullptr) {
            row.instrumentButton->setBounds(bounds.removeFromTop(18));
        }

        auto muteSoloArea = bounds.removeFromTop(18);
        row.muteButton->setBounds(muteSoloArea.removeFromLeft(muteSoloArea.getWidth() / 2));
        row.soloButton->setBounds(muteSoloArea);
    }

    m_addTrackButton.setBounds(0, getHeight() - kAddButtonHeight, getWidth(), kAddButtonHeight);
}

// Draws lane separators matching ArrangeView's grid
void TrackHeaderPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::darkgrey.darker());

    const float height = laneHeight();
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    for (std::size_t i = 1; i < m_rows.size(); ++i) {
        const auto y = static_cast<int>(static_cast<float>(i) * height);
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
}

// Rebuilds the rows from the model
void TrackHeaderPanel::refreshFromModel() {
    m_rows.clear();

    const std::size_t numTracks = m_arrangement.numTracks();
    for (std::size_t i = 0; i < numTracks; ++i) {
        model::Track& track = m_arrangement.track(i);
        Row row;

        row.nameLabel = std::make_unique<NameLabel>();
        row.nameLabel->setText(track.name, juce::dontSendNotification);
        row.nameLabel->setEditable(false, true, false);
        row.nameLabel->setColour(juce::Label::textColourId, juce::Colours::white);
        row.nameLabel->onTextChange = [this, i] {
            m_arrangement.track(i).name = m_rows[i].nameLabel->getText().toStdString();
        };
        row.nameLabel->onRightClick = [this, i] {
            showRemoveTrackMenu(i);
        };
        addAndMakeVisible(*row.nameLabel);

        if (track.kind == model::TrackKind::Midi) {
            row.instrumentButton = std::make_unique<juce::TextButton>();
            row.instrumentButton->setButtonText(instrumentNameFor ? instrumentNameFor(i) : juce::String());
            row.instrumentButton->onClick = [this, i] {
                if (onInstrumentPickRequested) {
                    onInstrumentPickRequested(i);
                }
            };
            addAndMakeVisible(*row.instrumentButton);
        }

        row.muteButton = std::make_unique<juce::TextButton>("M");
        row.muteButton->setClickingTogglesState(true);
        row.muteButton->setToggleState(m_mixer.trackStrip(i).muted(), juce::dontSendNotification);
        juce::TextButton* muteButtonPtr = row.muteButton.get();
        row.muteButton->onClick = [this, i, muteButtonPtr] {
            m_mixer.trackStrip(i).setMuted(muteButtonPtr->getToggleState());
        };
        addAndMakeVisible(*row.muteButton);

        row.soloButton = std::make_unique<juce::TextButton>("S");
        row.soloButton->setClickingTogglesState(true);
        row.soloButton->setToggleState(m_mixer.trackStrip(i).soloed(), juce::dontSendNotification);
        juce::TextButton* soloButtonPtr = row.soloButton.get();
        row.soloButton->onClick = [this, i, soloButtonPtr] {
            m_mixer.trackStrip(i).setSoloed(soloButtonPtr->getToggleState());
        };
        addAndMakeVisible(*row.soloButton);

        m_rows.push_back(std::move(row));
    }

    resized();
}

// 30 Hz, mirrors mute/solo toggle state from the mixer
void TrackHeaderPanel::timerCallback() {
    for (std::size_t i = 0; i < m_rows.size() && i < m_arrangement.numTracks(); ++i) {
        m_rows[i].muteButton->setToggleState(m_mixer.trackStrip(i).muted(), juce::dontSendNotification);
        m_rows[i].soloButton->setToggleState(m_mixer.trackStrip(i).soloed(), juce::dontSendNotification);
    }
}

// Opens the Remove Track confirmation menu for the given row
void TrackHeaderPanel::showRemoveTrackMenu(std::size_t trackIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, "Remove Track");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex](int result) {
        if (result != 1) {
            return;
        }

        m_commandStack.perform(std::make_unique<model::RemoveTrackCommand>(m_arrangement, m_mixer, trackIndex));
        refreshFromModel();

        if (onTracksChanged) {
            onTracksChanged();
        }
    });
}

// Opens the MIDI/Audio track-kind menu for the add-track button
void TrackHeaderPanel::showAddTrackMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "MIDI Track");
    menu.addItem(2, "Audio Track");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result != 1 && result != 2) {
            return;
        }

        const model::TrackKind kind = result == 1 ? model::TrackKind::Midi : model::TrackKind::Audio;
        const juce::String name = "Track " + juce::String(m_arrangement.numTracks() + 1);

        m_commandStack.perform(std::make_unique<model::AddTrackCommand>(
            m_arrangement, m_mixer, name.toStdString(), kind));
        refreshFromModel();

        if (onTracksChanged) {
            onTracksChanged();
        }
    });
}

} // namespace howl::ui
