// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: left column, one header row per track, aligned with the arrange view's lanes

#pragma once

#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/Mixer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace howl::ui {

// Left column: one header row per track, aligned with the arrange view's lanes
class TrackHeaderPanel : public juce::Component, private juce::Timer {
public:
    static constexpr int kWidth = 150;

    // Stores references, starts the 30 Hz mute/solo mirroring timer
    TrackHeaderPanel(model::Arrangement& arrangement, model::Mixer& mixer, model::CommandStack& commandStack);

    // Stops the timer
    ~TrackHeaderPanel() override;

    // Fired after any track add/remove so the app rebuilds the audio graph and views
    std::function<void()> onTracksChanged;

    // Fired when a MIDI track's instrument button is clicked, the app shows the picker
    std::function<void(std::size_t)> onInstrumentPickRequested;

    // App-provided display name for a track's current instrument
    std::function<juce::String(std::size_t)> instrumentNameFor;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Rebuilds the rows from the model
    void refreshFromModel();

private:
    static constexpr int kAddButtonHeight = 24;

    // A name label that reports right-clicks instead of only handling edit gestures
    class NameLabel : public juce::Label {
    public:
        std::function<void()> onRightClick;
        void mouseDown(const juce::MouseEvent& event) override;
    };

    // One track's header controls
    struct Row {
        std::unique_ptr<NameLabel> nameLabel;
        std::unique_ptr<juce::TextButton> instrumentButton; // MIDI tracks only
        std::unique_ptr<juce::TextButton> muteButton;
        std::unique_ptr<juce::TextButton> soloButton;
    };

    // 30 Hz, mirrors mute/solo toggle state from the mixer
    void timerCallback() override;

    // Opens the Remove Track confirmation menu for the given row
    void showRemoveTrackMenu(std::size_t trackIndex);

    // Opens the MIDI/Audio track-kind menu for the add-track button
    void showAddTrackMenu();

    // Returns the height of one track lane, matching ArrangeView's lane math
    float laneHeight() const;

    model::Arrangement& m_arrangement;
    model::Mixer& m_mixer;
    model::CommandStack& m_commandStack;

    std::vector<Row> m_rows;
    juce::TextButton m_addTrackButton { "+ Track" };
};

} // namespace howl::ui
