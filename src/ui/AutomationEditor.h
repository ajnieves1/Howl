// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: bottom panel editor for one track's automation lanes, one lane visible at a time

#pragma once

#include "model/Arrangement.h"
#include "model/CommandStack.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace howl::ui {

// v1 scope: one lane visible at a time, picked from a parameter combo built through the app's
// parameterNames provider. Choosing a parameter with no lane yet creates one. Dragging a point
// only previews its new position live, the model is not touched until mouseUp performs one
// MoveAutomationPointCommand, matching ArrangeView's clip drag gesture. No zoom, instrument
// parameters only
class AutomationEditor : public juce::Component {
public:
    // Stores references and builds the parameter combo from the provider
    AutomationEditor(model::Arrangement& arrangement, model::CommandStack& commandStack,
                     std::size_t trackIndex,
                     std::function<std::vector<juce::String>(std::size_t)> parameterNames);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Adds a point on an empty canvas click, begins a point drag, or deletes a point on right-click
    void mouseDown(const juce::MouseEvent& event) override;

    // Updates the live drag preview, does not touch the model
    void mouseDrag(const juce::MouseEvent& event) override;

    // Commits one MoveAutomationPointCommand for a completed drag
    void mouseUp(const juce::MouseEvent& event) override;

private:
    static constexpr int kLeftStripWidth = 120;
    static constexpr float kPointRadius = 3.0f;
    static constexpr int kHitRadiusPixels = 8;
    static constexpr int64_t kMinimumVisibleTicks = model::kTicksPerQuarter * 4 * 8; // 8 bars at 4/4

    // Returns the index into the track's automation vector for paramIndex, or -1
    int findLaneIndex(int paramIndex) const;

    // Ticks spanned by the canvas: the greater of the track's last clip end and 8 bars
    int64_t visibleTickSpan() const;

    // Converts a pixel x within the canvas to a tick, clamped to the visible span
    int64_t xToTick(int x) const;

    // Converts a tick to a pixel x within the canvas
    float tickToX(int64_t tick) const;

    // Converts a pixel y within the canvas to a normalized 0..1 value, bottom to top
    float yToValue(int y) const;

    // Converts a normalized 0..1 value to a pixel y within the canvas, bottom to top
    float valueToY(float value) const;

    // Returns the canvas area, the component bounds minus the left strip
    juce::Rectangle<int> canvasBounds() const;

    // Returns the index of the current lane's point under (x, y), or -1
    int hitTestPoint(int x, int y) const;

    // Rebuilds the parameter combo from the provider
    void rebuildParameterCombo();

    // Selects paramIndex as the visible lane, creating one first if none exists yet
    void selectParameter(int paramIndex);

    model::Arrangement& m_arrangement;
    model::CommandStack& m_commandStack;
    std::size_t m_trackIndex;
    std::function<std::vector<juce::String>(std::size_t)> m_parameterNames;

    juce::ComboBox m_parameterCombo;
    juce::TextButton m_deleteLaneButton { "Delete Lane" };

    int m_currentLaneIndex = -1;

    bool m_draggingPoint = false;
    int m_dragPointIndex = -1;
    model::AutomationPoint m_dragOriginalPoint {};
    model::AutomationPoint m_dragPreviewPoint {};
};

}
