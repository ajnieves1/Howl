// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the launch grid, one row per track aligned with the header panel, one column per scene

#pragma once

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"
#include "model/CommandStack.h"
#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <functional>

namespace howl::ui {

// The launch grid, one row per track aligned with the header panel, one column per scene
class SessionView : public juce::Component, private juce::Timer {
public:
    SessionView(model::Session& session, model::Arrangement& arrangement, model::ArrangementNode& node,
                engine::Transport& transport, model::CommandStack& commandStack);

    // Stops the state-poll timer
    ~SessionView() override;

    // Fired when a MIDI slot is double-clicked so the shell opens its piano roll
    std::function<void(std::size_t, std::size_t)> onSlotEditRequested;

    // Fired after any structural session edit so the shell refreshes views
    std::function<void()> onSessionEdited;

    // Nothing to lay out, every cell is custom-painted
    void resized() override;

    // Draws the header row (scene launch triangles, stop-all, add-scene) and the track/scene grid
    void paint(juce::Graphics& g) override;

    // Click launches or stops, right-click opens the slot menu
    void mouseDown(const juce::MouseEvent& event) override;

    // Double-click on an empty MIDI-track slot creates a clip and opens it
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // Rebuilds cached layout from the model after structural edits
    void refreshFromModel();

private:
    static constexpr int kSceneWidth = 90;
    static constexpr int kHeaderHeight = 20;

    // 30 Hz, repaints when any player's active or pending scene changed
    void timerCallback() override;

    // Returns the height of one track row, below the header, matching ArrangeView's lane math
    float laneHeight() const;

    // Converts a pixel y position to a track index, clamped to numTracks() - 1
    std::size_t yToTrackIndex(int y) const;

    // Opens a "Delete Clip" popup for the given slot
    void showDeleteSlotMenu(std::size_t trackIndex, std::size_t sceneIndex);

    model::Session& m_session;
    model::Arrangement& m_arrangement;
    model::ArrangementNode& m_node;
    engine::Transport& m_transport;
    model::CommandStack& m_commandStack;
};

} // namespace howl::ui
