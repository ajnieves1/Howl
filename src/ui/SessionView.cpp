// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the launch grid, one row per track aligned with the header panel, one column per scene

#include "ui/SessionView.h"

#include "model/Commands.h"
#include "model/MidiClip.h"

#include <memory>

namespace howl::ui {

// Stores references, starts the 30 Hz state-poll timer
SessionView::SessionView(model::Session& session, model::Arrangement& arrangement, model::ArrangementNode& node,
                          engine::Transport& transport, model::CommandStack& commandStack)
    : m_session(session)
    , m_arrangement(arrangement)
    , m_node(node)
    , m_transport(transport)
    , m_commandStack(commandStack)
{
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

// Stops the state-poll timer
SessionView::~SessionView() {
    stopTimer();
}

// Nothing to lay out, every cell is custom-painted
void SessionView::resized() {
}

// Returns the height of one track row, below the header, matching ArrangeView's lane math
float SessionView::laneHeight() const {
    const std::size_t numTracks = m_arrangement.numTracks();
    const float available = static_cast<float>(getHeight() - kHeaderHeight);
    if (numTracks == 0) {
        return available;
    }
    return available / static_cast<float>(numTracks);
}

// Converts a pixel y position to a track index, clamped to numTracks() - 1
std::size_t SessionView::yToTrackIndex(int y) const {
    const std::size_t numTracks = m_arrangement.numTracks();
    if (numTracks == 0) {
        return 0;
    }

    const float height = laneHeight();
    const int index = static_cast<int>(static_cast<float>(y - kHeaderHeight) / height);
    return static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(numTracks) - 1, index));
}

// Draws the header row (scene launch triangles, stop-all, add-scene) and the track/scene grid
void SessionView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    const std::size_t numTracks = m_arrangement.numTracks();
    const std::size_t numScenes = m_session.numScenes();

    g.setColour(juce::Colours::darkgrey.darker());
    g.fillRect(0, 0, getWidth(), kHeaderHeight);

    g.setColour(juce::Colours::white);
    g.drawText("Stop", 0, 0, kSceneWidth, kHeaderHeight, juce::Justification::centred);

    for (std::size_t scene = 0; scene < numScenes; ++scene) {
        const int x = kSceneWidth * static_cast<int>(scene + 1);
        g.drawText(juce::String::fromUTF8("\xE2\x96\xB6"), x, 0, kSceneWidth, kHeaderHeight,
            juce::Justification::centred);
    }

    const int addColumnX = kSceneWidth * static_cast<int>(numScenes + 1);
    g.drawText("+", addColumnX, 0, kSceneWidth, kHeaderHeight, juce::Justification::centred);

    if (numTracks == 0) {
        return;
    }

    const float height = laneHeight();

    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    for (std::size_t i = 1; i < numTracks; ++i) {
        const auto y = kHeaderHeight + static_cast<float>(i) * height;
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
    }

    for (std::size_t track = 0; track < numTracks; ++track) {
        const float y = kHeaderHeight + static_cast<float>(track) * height;
        const bool hasColumn = track < m_session.numTracks();
        const int activeScene = m_node.activeScene(track);
        const int pendingScene = m_node.pendingScene(track);

        for (std::size_t scene = 0; scene < numScenes; ++scene) {
            const int x = kSceneWidth * static_cast<int>(scene + 1);
            const juce::Rectangle<float> cell { static_cast<float>(x) + 2.0f, y + 2.0f,
                static_cast<float>(kSceneWidth) - 4.0f, height - 4.0f };

            const bool hasSlot = hasColumn && m_session.slot(track, scene).content != model::SlotContent::Empty;
            const bool isPlaying = activeScene == static_cast<int>(scene);
            const bool isPending = pendingScene == static_cast<int>(scene);

            juce::Colour fill = juce::Colours::darkgrey.darker();
            if (hasSlot) {
                const model::SlotContent content = m_session.slot(track, scene).content;
                fill = content == model::SlotContent::Midi ? juce::Colours::orange : juce::Colours::steelblue;
                if (isPlaying) {
                    fill = fill.brighter(0.6f);
                }
            }

            g.setColour(fill);
            g.fillRect(cell);

            g.setColour(isPending ? juce::Colours::yellow : fill.darker(0.8f));
            g.drawRect(cell, isPending ? 2.0f : 1.0f);

            if (hasSlot) {
                const model::SlotContent content = m_session.slot(track, scene).content;
                const juce::String label = content == model::SlotContent::Midi ? "M" : "A";

                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.drawText(label, cell.getSmallestIntegerContainer(), juce::Justification::topLeft);

                if (isPlaying) {
                    g.drawText(juce::String::fromUTF8("\xE2\x96\xB6"), cell.getSmallestIntegerContainer(),
                        juce::Justification::centred);
                }
            }
        }

        g.setColour(juce::Colours::grey.withAlpha(0.3f));
        const juce::Rectangle<float> addCell { static_cast<float>(addColumnX) + 2.0f, y + 2.0f,
            static_cast<float>(kSceneWidth) - 4.0f, height - 4.0f };
        g.drawRect(addCell, 1.0f);
    }
}

// Click launches or stops, right-click opens the slot menu
void SessionView::mouseDown(const juce::MouseEvent& event) {
    const int columnIndex = event.x / kSceneWidth;
    const std::size_t numScenes = m_session.numScenes();

    if (columnIndex == static_cast<int>(numScenes) + 1) {
        m_session.addScene();
        refreshFromModel();
        return;
    }

    if (columnIndex <= 0 || columnIndex > static_cast<int>(numScenes)) {
        if (columnIndex == 0 && event.y < kHeaderHeight) {
            for (std::size_t track = 0; track < m_arrangement.numTracks(); ++track) {
                if (m_node.activeScene(track) != -1) {
                    m_node.requestStop(track);
                }
            }
        }
        return;
    }

    const auto sceneIndex = static_cast<std::size_t>(columnIndex - 1);

    if (event.y < kHeaderHeight) {
        bool launchedAny = false;
        for (std::size_t track = 0; track < m_arrangement.numTracks(); ++track) {
            if (track < m_session.numTracks() && m_session.slot(track, sceneIndex).content != model::SlotContent::Empty) {
                if (!launchedAny && !m_transport.isPlaying()) {
                    m_transport.play();
                }
                launchedAny = true;
                m_node.requestLaunch(track, sceneIndex);
            }
        }
        return;
    }

    const std::size_t trackIndex = yToTrackIndex(event.y);
    if (trackIndex >= m_session.numTracks()) {
        return;
    }

    const model::ClipSlot& slot = m_session.slot(trackIndex, sceneIndex);

    if (event.mods.isPopupMenu()) {
        if (slot.content != model::SlotContent::Empty) {
            showDeleteSlotMenu(trackIndex, sceneIndex);
        }
        return;
    }

    if (slot.content != model::SlotContent::Empty) {
        if (!m_transport.isPlaying()) {
            m_transport.play();
        }
        m_node.requestLaunch(trackIndex, sceneIndex);
    } else if (m_node.activeScene(trackIndex) != -1) {
        m_node.requestStop(trackIndex);
    }
}

// Double-click on an empty MIDI-track slot creates a clip and opens it
void SessionView::mouseDoubleClick(const juce::MouseEvent& event) {
    if (event.y < kHeaderHeight) {
        return;
    }

    const int columnIndex = event.x / kSceneWidth;
    const std::size_t numScenes = m_session.numScenes();
    if (columnIndex <= 0 || columnIndex > static_cast<int>(numScenes)) {
        return;
    }

    const auto sceneIndex = static_cast<std::size_t>(columnIndex - 1);
    const std::size_t trackIndex = yToTrackIndex(event.y);

    if (trackIndex >= m_arrangement.numTracks() || m_arrangement.track(trackIndex).kind != model::TrackKind::Midi) {
        return;
    }

    if (trackIndex >= m_session.numTracks() || m_session.slot(trackIndex, sceneIndex).content != model::SlotContent::Empty) {
        return;
    }

    model::MidiClip clip;
    clip.setLengthTicks(model::kTicksPerQuarter * 4);

    m_commandStack.perform(std::make_unique<model::AddSessionMidiClipCommand>(m_session, trackIndex, sceneIndex, clip));

    if (onSessionEdited) {
        onSessionEdited();
    }

    if (onSlotEditRequested) {
        onSlotEditRequested(trackIndex, sceneIndex);
    }
}

// Rebuilds cached layout from the model after structural edits
void SessionView::refreshFromModel() {
    repaint();
}

// 30 Hz, repaints when any player's active or pending scene changed
void SessionView::timerCallback() {
    repaint();
}

// Opens a "Delete Clip" popup for the given slot
void SessionView::showDeleteSlotMenu(std::size_t trackIndex, std::size_t sceneIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, "Delete Clip");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex, sceneIndex](int result) {
        if (result != 1) {
            return;
        }

        m_commandStack.perform(std::make_unique<model::ClearSessionSlotCommand>(m_session, trackIndex, sceneIndex));

        if (onSessionEdited) {
            onSessionEdited();
        }

        repaint();
    });
}

} // namespace howl::ui
