// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: left column, one header row per track, aligned with the arrange view's lanes

#include "ui/TrackHeaderPanel.h"

#include "model/Commands.h"
#include "ui/Theme.h"

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

// Reports right-clicks via onRightClick, otherwise behaves like a normal button
void TrackHeaderPanel::InstrumentButton::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) {
        if (onRightClick) {
            onRightClick();
        }
        return;
    }

    TextButton::mouseDown(event);
}

// Stores references, builds the initial rows, starts the 30 Hz mirroring timer
TrackHeaderPanel::TrackHeaderPanel(model::Arrangement& arrangement, model::Mixer& mixer, model::Session& session,
                                    model::PatternBank& patterns, model::CommandStack& commandStack)
    : m_arrangement(arrangement)
    , m_mixer(mixer)
    , m_session(session)
    , m_patterns(patterns)
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

// Draws lane separators matching ArrangeView's grid, a tint over frozen rows, and a
// highlight over the row selected for live MIDI input
void TrackHeaderPanel::paint(juce::Graphics& g) {
    g.fillAll(theme::kPanelBg);

    const float height = laneHeight();

    if (isTrackFrozen) {
        g.setColour(theme::kAudio.withAlpha(0.15f));
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            if (isTrackFrozen(i)) {
                const auto y = static_cast<float>(i) * height;
                g.fillRect(0.0f, y, static_cast<float>(getWidth()), height);
            }
        }
    }

    if (m_selectedTrack >= 0 && static_cast<std::size_t>(m_selectedTrack) < m_rows.size()) {
        g.setColour(theme::kAccent.withAlpha(0.2f));
        const auto y = static_cast<float>(m_selectedTrack) * height;
        g.fillRect(0.0f, y, static_cast<float>(getWidth()), height);
    }

    g.setColour(theme::kBorder.withAlpha(0.4f));
    for (std::size_t i = 1; i < m_rows.size(); ++i) {
        const auto y = static_cast<int>(static_cast<float>(i) * height);
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
}

// Selects the row under the click as the live MIDI target
void TrackHeaderPanel::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu() || m_rows.empty()) {
        return;
    }

    const float height = laneHeight();
    if (height <= 0.0f) {
        return;
    }

    const auto row = static_cast<std::size_t>(static_cast<float>(event.y) / height);
    if (row >= m_rows.size()) {
        return;
    }

    m_selectedTrack = static_cast<std::ptrdiff_t>(row);
    repaint();

    if (onTrackSelected) {
        onTrackSelected(m_selectedTrack);
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
        row.nameLabel->setColour(juce::Label::textColourId, theme::kTextPrimary);
        row.nameLabel->onTextChange = [this, i] {
            m_arrangement.track(i).name = m_rows[i].nameLabel->getText().toStdString();
        };
        row.nameLabel->onRightClick = [this, i] {
            showRemoveTrackMenu(i);
        };
        addAndMakeVisible(*row.nameLabel);

        if (track.kind == model::TrackKind::Midi) {
            row.instrumentButton = std::make_unique<InstrumentButton>();
            const juce::String initialInstrumentName = instrumentNameFor ? instrumentNameFor(i) : juce::String();
            row.instrumentButton->setButtonText(initialInstrumentName);
            row.instrumentButton->setTooltip(initialInstrumentName + " (right click for options)");
            row.instrumentButton->onClick = [this, i] {
                if (onInstrumentPickRequested) {
                    onInstrumentPickRequested(i);
                }
            };
            row.instrumentButton->onRightClick = [this, i] {
                showInstrumentMenu(i);
            };
            addAndMakeVisible(*row.instrumentButton);
        }

        row.muteButton = std::make_unique<juce::TextButton>("M");
        row.muteButton->setClickingTogglesState(true);
        row.muteButton->setToggleState(m_mixer.trackStrip(i).muted(), juce::dontSendNotification);
        row.muteButton->setColour(juce::TextButton::buttonOnColourId, theme::kAccent);
        row.muteButton->setColour(juce::TextButton::textColourOffId, theme::kTextSecondary);
        row.muteButton->setTooltip("Mute track");
        juce::TextButton* muteButtonPtr = row.muteButton.get();
        row.muteButton->onClick = [this, i, muteButtonPtr] {
            m_mixer.trackStrip(i).setMuted(muteButtonPtr->getToggleState());
        };
        addAndMakeVisible(*row.muteButton);

        row.soloButton = std::make_unique<juce::TextButton>("S");
        row.soloButton->setClickingTogglesState(true);
        row.soloButton->setToggleState(m_mixer.trackStrip(i).soloed(), juce::dontSendNotification);
        row.soloButton->setColour(juce::TextButton::buttonOnColourId, theme::kAudio);
        row.soloButton->setColour(juce::TextButton::textColourOffId, theme::kTextSecondary);
        row.soloButton->setTooltip("Solo track");
        juce::TextButton* soloButtonPtr = row.soloButton.get();
        row.soloButton->onClick = [this, i, soloButtonPtr] {
            m_mixer.trackStrip(i).setSoloed(soloButtonPtr->getToggleState());
        };
        addAndMakeVisible(*row.soloButton);

        m_rows.push_back(std::move(row));
    }

    resized();
    repaint(); // picks up the frozen-row tint immediately after a freeze/unfreeze
}

// 30 Hz, mirrors mute/solo toggle state from the mixer, and a crashed instrument's
// red "(crashed)" label, the same cadence and treatment as a crashed FX chain row
void TrackHeaderPanel::timerCallback() {
    for (std::size_t i = 0; i < m_rows.size() && i < m_arrangement.numTracks(); ++i) {
        m_rows[i].muteButton->setToggleState(m_mixer.trackStrip(i).muted(), juce::dontSendNotification);
        m_rows[i].soloButton->setToggleState(m_mixer.trackStrip(i).soloed(), juce::dontSendNotification);

        if (m_rows[i].instrumentButton == nullptr) {
            continue;
        }

        const bool crashed = isInstrumentCrashed && isInstrumentCrashed(i);
        juce::String text = instrumentNameFor ? instrumentNameFor(i) : juce::String();
        if (crashed) {
            text << " (crashed)";
            m_rows[i].instrumentButton->setColour(juce::TextButton::textColourOffId, theme::kDanger);
        } else {
            m_rows[i].instrumentButton->removeColour(juce::TextButton::textColourOffId);
        }
        m_rows[i].instrumentButton->setButtonText(text);
        m_rows[i].instrumentButton->setTooltip(text + " (right click for options)");
    }
}

// Opens the Remove Track confirmation menu for the given row, plus Freeze/Unfreeze and Automation
void TrackHeaderPanel::showRemoveTrackMenu(std::size_t trackIndex) {
    const bool frozen = isTrackFrozen && isTrackFrozen(trackIndex);

    juce::PopupMenu menu;
    menu.addItem(2, frozen ? "Unfreeze Track" : "Freeze Track");
    menu.addItem(3, "Automation...");
    menu.addSeparator();
    menu.addItem(1, "Remove Track");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex, frozen](int result) {
        if (result == 1) {
            m_commandStack.perform(std::make_unique<model::RemoveTrackCommand>(
                m_arrangement, m_mixer, m_session, m_patterns, trackIndex));
            refreshFromModel();

            m_selectedTrack = -1;
            if (onTrackSelected) {
                onTrackSelected(-1);
            }

            if (onTracksChanged) {
                onTracksChanged();
            }
        } else if (result == 2) {
            if (onFreezeRequested) {
                onFreezeRequested(trackIndex, !frozen);
            }
        } else if (result == 3) {
            if (onAutomationRequested) {
                onAutomationRequested(trackIndex);
            }
        }
    });
}

// Opens the Change Instrument.../Open Editor menu for the given row's instrument button,
// plus Restart Plugin when the instrument has crashed
void TrackHeaderPanel::showInstrumentMenu(std::size_t trackIndex) {
    const bool crashed = isInstrumentCrashed && isInstrumentCrashed(trackIndex);

    juce::PopupMenu menu;
    menu.addItem(1, "Change Instrument...");
    menu.addItem(2, "Open Editor");
    if (crashed) {
        menu.addItem(3, "Restart Plugin");
    }

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex](int result) {
        if (result == 1) {
            if (onInstrumentPickRequested) {
                onInstrumentPickRequested(trackIndex);
            }
        } else if (result == 2) {
            if (onInstrumentEditRequested) {
                onInstrumentEditRequested(trackIndex);
            }
        } else if (result == 3) {
            if (onRestartInstrumentRequested) {
                onRestartInstrumentRequested(trackIndex);
            }
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
            m_arrangement, m_mixer, m_session, m_patterns, name.toStdString(), kind));
        refreshFromModel();

        m_selectedTrack = -1;
        if (onTrackSelected) {
            onTrackSelected(-1);
        }

        if (onTracksChanged) {
            onTracksChanged();
        }
    });
}

} // namespace howl::ui
