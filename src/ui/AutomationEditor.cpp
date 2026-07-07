// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: bottom panel editor for one track's automation lanes, one lane visible at a time

#include "ui/AutomationEditor.h"

#include "model/Commands.h"

#include <algorithm>
#include <cmath>

namespace howl::ui {

namespace {
constexpr int kComboHeight = 24;
constexpr int kButtonHeight = 24;
constexpr int kControlSpacing = 4;
}

// Stores references and builds the parameter combo from the provider
AutomationEditor::AutomationEditor(model::Arrangement& arrangement, model::CommandStack& commandStack,
                                    std::size_t trackIndex,
                                    std::function<std::vector<juce::String>(std::size_t)> parameterNames)
    : m_arrangement(arrangement)
    , m_commandStack(commandStack)
    , m_trackIndex(trackIndex)
    , m_parameterNames(std::move(parameterNames))
{
    addAndMakeVisible(m_parameterCombo);
    m_parameterCombo.onChange = [this] {
        const int selectedId = m_parameterCombo.getSelectedId();
        if (selectedId > 0) {
            selectParameter(selectedId - 1);
        }
    };

    addAndMakeVisible(m_deleteLaneButton);
    m_deleteLaneButton.onClick = [this] {
        if (m_currentLaneIndex < 0) {
            return;
        }

        m_commandStack.perform(std::make_unique<model::RemoveAutomationLaneCommand>(
            m_arrangement, m_trackIndex, static_cast<std::size_t>(m_currentLaneIndex)));
        m_currentLaneIndex = -1;
        rebuildParameterCombo();
        repaint();
    };

    rebuildParameterCombo();
}

// Lays the parameter combo and delete button in the left strip
void AutomationEditor::resized() {
    auto bounds = getLocalBounds();
    auto leftStrip = bounds.removeFromLeft(kLeftStripWidth).reduced(4);

    m_parameterCombo.setBounds(leftStrip.removeFromTop(kComboHeight));
    leftStrip.removeFromTop(kControlSpacing);
    m_deleteLaneButton.setBounds(leftStrip.removeFromTop(kButtonHeight));
}

// Draws the canvas border, then the current lane's points joined by straight lines
void AutomationEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    const auto canvas = canvasBounds();
    g.setColour(juce::Colours::grey.withAlpha(0.25f));
    g.drawRect(canvas);

    if (m_currentLaneIndex < 0) {
        return;
    }

    const auto& points = m_arrangement.track(m_trackIndex)
        .automation[static_cast<std::size_t>(m_currentLaneIndex)].lane.points();

    juce::Path path;
    for (std::size_t i = 0; i < points.size(); ++i) {
        model::AutomationPoint point = points[i];
        if (m_draggingPoint && static_cast<int>(i) == m_dragPointIndex) {
            point = m_dragPreviewPoint;
        }

        const float x = tickToX(point.tick);
        const float y = valueToY(point.value);

        if (i == 0) {
            path.startNewSubPath(x, y);
        } else {
            path.lineTo(x, y);
        }
    }

    g.setColour(juce::Colours::orange);
    g.strokePath(path, juce::PathStrokeType(1.5f));

    for (std::size_t i = 0; i < points.size(); ++i) {
        model::AutomationPoint point = points[i];
        if (m_draggingPoint && static_cast<int>(i) == m_dragPointIndex) {
            point = m_dragPreviewPoint;
        }

        const float x = tickToX(point.tick);
        const float y = valueToY(point.value);
        g.fillEllipse(x - kPointRadius, y - kPointRadius, kPointRadius * 2.0f, kPointRadius * 2.0f);
    }
}

// Adds a point on an empty canvas click, begins a point drag, or deletes a point on right-click
void AutomationEditor::mouseDown(const juce::MouseEvent& event) {
    if (m_currentLaneIndex < 0) {
        return;
    }

    const auto canvas = canvasBounds();
    if (!canvas.contains(event.getPosition())) {
        return;
    }

    const int pointIndex = hitTestPoint(event.x, event.y);

    if (event.mods.isPopupMenu()) {
        if (pointIndex >= 0) {
            m_commandStack.perform(std::make_unique<model::RemoveAutomationPointCommand>(
                m_arrangement, m_trackIndex, static_cast<std::size_t>(m_currentLaneIndex),
                static_cast<std::size_t>(pointIndex)));
            repaint();
        }
        return;
    }

    if (pointIndex >= 0) {
        const auto& lane = m_arrangement.track(m_trackIndex)
            .automation[static_cast<std::size_t>(m_currentLaneIndex)].lane;

        m_draggingPoint = true;
        m_dragPointIndex = pointIndex;
        m_dragOriginalPoint = lane.points()[static_cast<std::size_t>(pointIndex)];
        m_dragPreviewPoint = m_dragOriginalPoint;
        return;
    }

    const int64_t rawTick = xToTick(event.x);
    const int64_t snappedTick = ((rawTick + model::kTicksPerQuarter / 2) / model::kTicksPerQuarter)
        * model::kTicksPerQuarter;
    const float value = yToValue(event.y);

    m_commandStack.perform(std::make_unique<model::AddAutomationPointCommand>(
        m_arrangement, m_trackIndex, static_cast<std::size_t>(m_currentLaneIndex),
        model::AutomationPoint { snappedTick, value }));
    repaint();
}

// Updates the live drag preview, does not touch the model
void AutomationEditor::mouseDrag(const juce::MouseEvent& event) {
    if (!m_draggingPoint) {
        return;
    }

    const auto canvas = canvasBounds();
    const int clampedX = juce::jlimit(canvas.getX(), canvas.getRight(), event.x);
    const int clampedY = juce::jlimit(canvas.getY(), canvas.getBottom(), event.y);

    const int64_t rawTick = xToTick(clampedX);
    const int64_t snappedTick = ((rawTick + model::kTicksPerQuarter / 2) / model::kTicksPerQuarter)
        * model::kTicksPerQuarter;

    m_dragPreviewPoint.tick = snappedTick;
    m_dragPreviewPoint.value = yToValue(clampedY);
    repaint();
}

// Commits one MoveAutomationPointCommand for a completed drag
void AutomationEditor::mouseUp(const juce::MouseEvent&) {
    if (!m_draggingPoint) {
        return;
    }

    m_draggingPoint = false;

    constexpr float kValueEpsilon = 1e-6f;
    const bool tickMoved = m_dragPreviewPoint.tick != m_dragOriginalPoint.tick;
    const bool valueMoved = std::fabs(m_dragPreviewPoint.value - m_dragOriginalPoint.value) > kValueEpsilon;

    if (tickMoved || valueMoved) {
        m_commandStack.perform(std::make_unique<model::MoveAutomationPointCommand>(
            m_arrangement, m_trackIndex, static_cast<std::size_t>(m_currentLaneIndex),
            m_dragOriginalPoint, m_dragPreviewPoint));
    }

    m_dragPointIndex = -1;
    repaint();
}

// Returns the index into the track's automation vector for paramIndex, or -1
int AutomationEditor::findLaneIndex(int paramIndex) const {
    const auto& automation = m_arrangement.track(m_trackIndex).automation;
    for (std::size_t i = 0; i < automation.size(); ++i) {
        if (automation[i].paramIndex == paramIndex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Ticks spanned by the canvas: the greater of the track's last MIDI clip end and 8 bars
int64_t AutomationEditor::visibleTickSpan() const {
    int64_t lastClipEnd = 0;
    for (const auto& placement : m_arrangement.track(m_trackIndex).midiClips) {
        lastClipEnd = std::max(lastClipEnd, placement.startTick + placement.clip.lengthTicks());
    }
    return std::max(lastClipEnd, kMinimumVisibleTicks);
}

// Converts a pixel x within the canvas to a tick, clamped to the visible span
int64_t AutomationEditor::xToTick(int x) const {
    const auto canvas = canvasBounds();
    const float ratio = canvas.getWidth() > 0
        ? static_cast<float>(x - canvas.getX()) / static_cast<float>(canvas.getWidth())
        : 0.0f;
    const float clampedRatio = juce::jlimit(0.0f, 1.0f, ratio);
    return static_cast<int64_t>(clampedRatio * static_cast<float>(visibleTickSpan()));
}

// Converts a tick to a pixel x within the canvas
float AutomationEditor::tickToX(int64_t tick) const {
    const auto canvas = canvasBounds();
    const int64_t span = visibleTickSpan();
    const float ratio = span > 0 ? static_cast<float>(tick) / static_cast<float>(span) : 0.0f;
    return static_cast<float>(canvas.getX()) + ratio * static_cast<float>(canvas.getWidth());
}

// Converts a pixel y within the canvas to a normalized 0..1 value, bottom to top
float AutomationEditor::yToValue(int y) const {
    const auto canvas = canvasBounds();
    if (canvas.getHeight() <= 0) {
        return 0.0f;
    }
    const float ratio = static_cast<float>(canvas.getBottom() - y) / static_cast<float>(canvas.getHeight());
    return juce::jlimit(0.0f, 1.0f, ratio);
}

// Converts a normalized 0..1 value to a pixel y within the canvas, bottom to top
float AutomationEditor::valueToY(float value) const {
    const auto canvas = canvasBounds();
    const float clamped = juce::jlimit(0.0f, 1.0f, value);
    return static_cast<float>(canvas.getBottom()) - clamped * static_cast<float>(canvas.getHeight());
}

// Returns the canvas area, the component bounds minus the left strip
juce::Rectangle<int> AutomationEditor::canvasBounds() const {
    return getLocalBounds().withTrimmedLeft(kLeftStripWidth);
}

// Returns the index of the current lane's point under (x, y), or -1
int AutomationEditor::hitTestPoint(int x, int y) const {
    if (m_currentLaneIndex < 0) {
        return -1;
    }

    const auto& points = m_arrangement.track(m_trackIndex)
        .automation[static_cast<std::size_t>(m_currentLaneIndex)].lane.points();

    for (std::size_t i = 0; i < points.size(); ++i) {
        model::AutomationPoint point = points[i];
        if (m_draggingPoint && static_cast<int>(i) == m_dragPointIndex) {
            point = m_dragPreviewPoint;
        }

        const float px = tickToX(point.tick);
        const float py = valueToY(point.value);
        const float dx = px - static_cast<float>(x);
        const float dy = py - static_cast<float>(y);

        if (dx * dx + dy * dy <= static_cast<float>(kHitRadiusPixels * kHitRadiusPixels)) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

// Rebuilds the parameter combo from the provider
void AutomationEditor::rebuildParameterCombo() {
    m_parameterCombo.clear(juce::dontSendNotification);

    const std::vector<juce::String> names = m_parameterNames
        ? m_parameterNames(m_trackIndex)
        : std::vector<juce::String> {};

    for (std::size_t i = 0; i < names.size(); ++i) {
        m_parameterCombo.addItem(names[i], static_cast<int>(i) + 1);
    }

    if (m_currentLaneIndex >= 0) {
        const auto& automation = m_arrangement.track(m_trackIndex).automation;
        if (static_cast<std::size_t>(m_currentLaneIndex) < automation.size()) {
            const int paramIndex = automation[static_cast<std::size_t>(m_currentLaneIndex)].paramIndex;
            m_parameterCombo.setSelectedId(paramIndex + 1, juce::dontSendNotification);
        }
    }
}

// Selects paramIndex as the visible lane, creating one first if none exists yet
void AutomationEditor::selectParameter(int paramIndex) {
    int laneIndex = findLaneIndex(paramIndex);
    if (laneIndex < 0) {
        m_commandStack.perform(
            std::make_unique<model::AddAutomationLaneCommand>(m_arrangement, m_trackIndex, paramIndex));
        laneIndex = findLaneIndex(paramIndex);
    }

    m_currentLaneIndex = laneIndex;
    repaint();
}

} // namespace howl::ui
