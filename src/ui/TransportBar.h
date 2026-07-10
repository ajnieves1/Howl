// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: play/stop, position readout, editable tempo, undo/redo, mixer toggle

#pragma once

#include "engine/Transport.h"
#include "model/CommandStack.h"
#include "model/SnapGrid.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace howl::ui {

// Play/stop, position readout, editable tempo, undo/redo, mixer toggle
class TransportBar : public juce::Component, private juce::Timer {
public:
    // Stores references, starts the 30 Hz UI refresh timer
    TransportBar(engine::Transport& transport, model::CommandStack& commandStack, double sampleRate);

    // Stops the timer
    ~TransportBar() override;

    // Fired when the Mixer button is clicked
    std::function<void()> onMixerToggle;

    // Fired after an undo or redo so the owner refreshes every view
    std::function<void()> onEditPerformed;

    // Fired after a tempo edit commits, so the owner can rewarp audio clips
    std::function<void()> onTempoCommitted;

    // Fired when the snap combo's selection changes
    std::function<void(model::SnapDivision)> onSnapChanged;

    // Fired when a view switcher segment is clicked: 0 Arrange, 1 Session, 2 Rack
    std::function<void(int)> onViewSelected;

    // Highlights the given view switcher segment (0 Arrange, 1 Session, 2 Rack) as active
    void setActiveView(int index);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // 30 Hz: reflects play state on the button and updates the position readout
    void timerCallback() override;

    // Parses and clamps the tempo label's text, applies it to the transport
    void commitTempo();

    // Maps a snap combo item id (1-based) to its division
    static model::SnapDivision snapDivisionForItemId(int itemId);

    engine::Transport& m_transport;
    model::CommandStack& m_commandStack;
    double m_sampleRate;

    juce::TextButton m_playButton { "Play" };
    juce::Label m_positionLabel;
    juce::Label m_tempoLabel;
    juce::TextButton m_undoButton { "Undo" };
    juce::TextButton m_redoButton { "Redo" };
    juce::TextButton m_mixerButton { "Mixer" };
    juce::ComboBox m_snapCombo;

    juce::TextButton m_arrangeViewButton { "Arrange" };
    juce::TextButton m_sessionViewButton { "Session" };
    juce::TextButton m_rackViewButton { "Rack" };
};

} // namespace howl::ui
