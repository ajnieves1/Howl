// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: play/stop, position readout, editable tempo, undo/redo, mixer toggle

#include "ui/TransportBar.h"

#include "ui/Theme.h"

namespace howl::ui {

// Stores references, starts the 30 Hz UI refresh timer
TransportBar::TransportBar(engine::Transport& transport, model::CommandStack& commandStack, double sampleRate)
    : m_transport(transport)
    , m_commandStack(commandStack)
    , m_sampleRate(sampleRate)
{
    m_playButton.onClick = [this] {
        if (m_transport.isPlaying()) {
            m_transport.stop();
        } else {
            m_transport.play();
        }
    };
    m_playButton.setTooltip("Play or stop (Space)");
    addAndMakeVisible(m_playButton);

    m_metronomeButton.setClickingTogglesState(true);
    m_metronomeButton.setTooltip("Metronome click during playback");
    m_metronomeButton.onClick = [this] {
        if (onMetronomeToggled) {
            onMetronomeToggled(m_metronomeButton.getToggleState());
        }
    };
    addAndMakeVisible(m_metronomeButton);

    m_countInButton.setClickingTogglesState(true);
    m_countInButton.setTooltip("Play one bar of clicks before playback starts");
    m_countInButton.onClick = [this] {
        if (onCountInToggled) {
            onCountInToggled(m_countInButton.getToggleState());
        }
    };
    addAndMakeVisible(m_countInButton);

    m_positionLabel.setJustificationType(juce::Justification::centred);
    m_positionLabel.setTooltip("Bar.beat and minutes:seconds");
    addAndMakeVisible(m_positionLabel);

    m_tempoLabel.setText(juce::String(m_transport.tempo(), 1), juce::dontSendNotification);
    m_tempoLabel.setEditable(true, true, false);
    m_tempoLabel.setJustificationType(juce::Justification::centred);
    m_tempoLabel.onTextChange = [this] {
        commitTempo();
    };
    m_tempoLabel.setTooltip("Tempo in BPM, click to edit");
    addAndMakeVisible(m_tempoLabel);

    m_undoButton.onClick = [this] {
        m_commandStack.undo();
        if (onEditPerformed) {
            onEditPerformed();
        }
    };
    m_undoButton.setTooltip("Undo (Ctrl+Z)");
    addAndMakeVisible(m_undoButton);

    m_redoButton.onClick = [this] {
        m_commandStack.redo();
        if (onEditPerformed) {
            onEditPerformed();
        }
    };
    m_redoButton.setTooltip("Redo (Ctrl+Shift+Z)");
    addAndMakeVisible(m_redoButton);

    m_mixerButton.onClick = [this] {
        if (onMixerToggle) {
            onMixerToggle();
        }
    };
    m_mixerButton.setTooltip("Toggle the mixer panel (M)");
    addAndMakeVisible(m_mixerButton);

    m_snapCombo.addItem("Bar", 1);
    m_snapCombo.addItem("Beat", 2);
    m_snapCombo.addItem("1/2 Beat", 3);
    m_snapCombo.addItem("Step", 4);
    m_snapCombo.addItem("Off", 5);
    m_snapCombo.setSelectedId(4, juce::dontSendNotification); // Step is the default division
    m_snapCombo.onChange = [this] {
        if (onSnapChanged) {
            onSnapChanged(snapDivisionForItemId(m_snapCombo.getSelectedId()));
        }
    };
    m_snapCombo.setTooltip("Snap division for dragging and creating clips");
    addAndMakeVisible(m_snapCombo);

    // A JUCE radio group keeps exactly one segment toggled at a time
    m_arrangeViewButton.setTooltip("Arrange view");
    m_sessionViewButton.setTooltip("Session view");
    m_rackViewButton.setTooltip("Channel rack view");
    for (auto* button : { &m_arrangeViewButton, &m_sessionViewButton, &m_rackViewButton }) {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(1);
        button->setColour(juce::TextButton::buttonOnColourId, theme::kAccent);
        button->setColour(juce::TextButton::textColourOffId, theme::kTextSecondary);
        addAndMakeVisible(*button);
    }
    m_arrangeViewButton.setToggleState(true, juce::dontSendNotification);
    m_arrangeViewButton.onClick = [this] {
        if (onViewSelected) {
            onViewSelected(0);
        }
    };
    m_sessionViewButton.onClick = [this] {
        if (onViewSelected) {
            onViewSelected(1);
        }
    };
    m_rackViewButton.onClick = [this] {
        if (onViewSelected) {
            onViewSelected(2);
        }
    };

    startTimerHz(30);
}

// Stops the timer
TransportBar::~TransportBar() {
    stopTimer();
}

// Lays the controls out left to right
void TransportBar::resized() {
    auto bounds = getLocalBounds().reduced(2);

    m_playButton.setBounds(bounds.removeFromLeft(60));
    m_metronomeButton.setBounds(bounds.removeFromLeft(56));
    m_countInButton.setBounds(bounds.removeFromLeft(70));
    m_positionLabel.setBounds(bounds.removeFromLeft(100));
    m_tempoLabel.setBounds(bounds.removeFromLeft(60));

    m_rackViewButton.setBounds(bounds.removeFromRight(70));
    m_sessionViewButton.setBounds(bounds.removeFromRight(70));
    m_arrangeViewButton.setBounds(bounds.removeFromRight(70));

    m_snapCombo.setBounds(bounds.removeFromRight(90));
    m_mixerButton.setBounds(bounds.removeFromRight(60));
    m_redoButton.setBounds(bounds.removeFromRight(60));
    m_undoButton.setBounds(bounds.removeFromRight(60));
}

// Sets the metronome toggle's shown state without firing its callback
void TransportBar::setMetronomeEnabled(bool enabled) {
    m_metronomeButton.setToggleState(enabled, juce::dontSendNotification);
}

// Sets the count in toggle's shown state without firing its callback
void TransportBar::setCountInEnabled(bool enabled) {
    m_countInButton.setToggleState(enabled, juce::dontSendNotification);
}

// Highlights the given view switcher segment (0 Arrange, 1 Session, 2 Rack) as active
void TransportBar::setActiveView(int index) {
    m_arrangeViewButton.setToggleState(index == 0, juce::dontSendNotification);
    m_sessionViewButton.setToggleState(index == 1, juce::dontSendNotification);
    m_rackViewButton.setToggleState(index == 2, juce::dontSendNotification);
}

// Fills the background
void TransportBar::paint(juce::Graphics& g) {
    g.fillAll(theme::kPanelBg);
}

// 30 Hz: reflects play state on the button and updates the position readout
void TransportBar::timerCallback() {
    m_playButton.setButtonText(m_transport.isPlaying() ? "Stop" : "Play");

    const double tempo = m_transport.tempo();
    const double samplesPerBeat = (60.0 / tempo) * m_sampleRate;
    const double totalBeats = static_cast<double>(m_transport.position()) / samplesPerBeat;
    const int bar = static_cast<int>(totalBeats / 4.0) + 1;
    const int beatInBar = static_cast<int>(totalBeats) % 4 + 1;

    const double totalSeconds = static_cast<double>(m_transport.position()) / m_sampleRate;
    const int minutes = static_cast<int>(totalSeconds / 60.0);
    const int seconds = static_cast<int>(totalSeconds) % 60;

    m_positionLabel.setText(
        juce::String(bar) + "." + juce::String(beatInBar) + "  "
            + juce::String(minutes) + ":" + juce::String(seconds).paddedLeft('0', 2),
        juce::dontSendNotification);

    m_undoButton.setEnabled(m_commandStack.canUndo());
    m_redoButton.setEnabled(m_commandStack.canRedo());
}

// Parses and clamps the tempo label's text, applies it to the transport
void TransportBar::commitTempo() {
    const double parsed = m_tempoLabel.getText().getDoubleValue();
    const double clamped = juce::jlimit(20.0, 300.0, parsed);
    m_transport.setTempo(clamped);
    m_tempoLabel.setText(juce::String(clamped, 1), juce::dontSendNotification);

    if (onTempoCommitted) {
        onTempoCommitted();
    }
}

// Maps a snap combo item id (1-based) to its division
model::SnapDivision TransportBar::snapDivisionForItemId(int itemId) {
    switch (itemId) {
        case 1:
            return model::SnapDivision::Bar;
        case 2:
            return model::SnapDivision::Beat;
        case 3:
            return model::SnapDivision::HalfBeat;
        case 5:
            return model::SnapDivision::Off;
        case 4:
        default:
            return model::SnapDivision::Step;
    }
}

} // namespace howl::ui
