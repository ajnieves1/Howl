// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: play/stop, position readout, editable tempo, undo/redo, mixer toggle

#include "ui/TransportBar.h"

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
    addAndMakeVisible(m_playButton);

    m_positionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_positionLabel);

    m_tempoLabel.setText(juce::String(m_transport.tempo(), 1), juce::dontSendNotification);
    m_tempoLabel.setEditable(true, true, false);
    m_tempoLabel.setJustificationType(juce::Justification::centred);
    m_tempoLabel.onTextChange = [this] {
        commitTempo();
    };
    addAndMakeVisible(m_tempoLabel);

    m_undoButton.onClick = [this] {
        m_commandStack.undo();
        if (onEditPerformed) {
            onEditPerformed();
        }
    };
    addAndMakeVisible(m_undoButton);

    m_redoButton.onClick = [this] {
        m_commandStack.redo();
        if (onEditPerformed) {
            onEditPerformed();
        }
    };
    addAndMakeVisible(m_redoButton);

    m_mixerButton.onClick = [this] {
        if (onMixerToggle) {
            onMixerToggle();
        }
    };
    addAndMakeVisible(m_mixerButton);

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
    m_positionLabel.setBounds(bounds.removeFromLeft(100));
    m_tempoLabel.setBounds(bounds.removeFromLeft(60));
    m_mixerButton.setBounds(bounds.removeFromRight(60));
    m_redoButton.setBounds(bounds.removeFromRight(60));
    m_undoButton.setBounds(bounds.removeFromRight(60));
}

// Fills the background
void TransportBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::darkgrey.darker());
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
}

} // namespace howl::ui
