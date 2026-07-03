// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the whole app shell, transport bar top, arrange view center, piano roll or mixer bottom

#include "ui/MainComponent.h"

namespace howl::ui {

// Builds the transport bar, arrange view, session view, and mixer, wires their callbacks
MainComponent::MainComponent(model::Arrangement& arrangement, engine::Transport& transport,
                              model::CommandStack& commandStack, model::Mixer& mixer, model::Session& session,
                              model::ArrangementNode& arrangementNode, engine::IEffectFactory& factory,
                              plugins::IPluginHost* pluginHost, double sampleRate, int maxBlockSize)
    : m_arrangement(arrangement)
    , m_transport(transport)
    , m_commandStack(commandStack)
    , m_session(session)
    , m_sampleRate(sampleRate)
    , m_transportBar(transport, commandStack, sampleRate)
    , m_trackHeaderPanel(arrangement, mixer, session, commandStack)
    , m_arrangeView(arrangement, transport, commandStack, sampleRate)
{
    // Without this, keyPressed never fires anywhere in the shell (the P5-T10 lesson)
    setWantsKeyboardFocus(true);

    addAndMakeVisible(m_transportBar);
    addAndMakeVisible(m_trackHeaderPanel);
    addAndMakeVisible(m_arrangeView);

    m_sessionView = std::make_unique<SessionView>(session, arrangement, arrangementNode, transport, commandStack);
    addChildComponent(*m_sessionView);
    m_sessionView->onSlotEditRequested = [this](std::size_t trackIndex, std::size_t sceneIndex) {
        showPianoRollForSession(trackIndex, sceneIndex);
    };
    m_sessionView->onSessionEdited = [this] {
        refreshAllViews();
    };

    m_trackHeaderPanel.onTracksChanged = [this] {
        if (onTracksChanged) {
            onTracksChanged();
        }
    };
    m_trackHeaderPanel.onInstrumentPickRequested = [this](std::size_t trackIndex) {
        if (onInstrumentPickRequested) {
            onInstrumentPickRequested(trackIndex);
        }
    };
    m_trackHeaderPanel.onInstrumentEditRequested = [this](std::size_t trackIndex) {
        if (onInstrumentEditRequested) {
            onInstrumentEditRequested(trackIndex);
        }
    };
    m_trackHeaderPanel.instrumentNameFor = [this](std::size_t trackIndex) -> juce::String {
        if (instrumentNameFor) {
            return instrumentNameFor(trackIndex);
        }
        return {};
    };
    m_trackHeaderPanel.isTrackFrozen = [this](std::size_t trackIndex) -> bool {
        if (isTrackFrozen) {
            return isTrackFrozen(trackIndex);
        }
        return false;
    };
    m_trackHeaderPanel.onFreezeRequested = [this](std::size_t trackIndex, bool freeze) {
        if (onFreezeRequested) {
            onFreezeRequested(trackIndex, freeze);
        }
    };

    m_arrangeView.onMidiClipSelected = [this](std::size_t trackIndex, std::size_t placementIndex) {
        showPianoRollFor(trackIndex, placementIndex);
    };
    m_arrangeView.onMixerRequested = [this] {
        toggleMixer();
    };
    m_arrangeView.onAudioFileDropped = [this](juce::String path, std::size_t trackIndex, int64_t tick) {
        if (onAudioFileDropped) {
            onAudioFileDropped(path, trackIndex, tick);
        }
    };
    m_arrangeView.onWarpChanged = [this] {
        if (onRewarpRequested) {
            onRewarpRequested();
        }
    };

    m_transportBar.onMixerToggle = [this] {
        toggleMixer();
    };
    m_transportBar.onEditPerformed = [this] {
        refreshAllViews();
    };
    m_transportBar.onTempoCommitted = [this] {
        if (onRewarpRequested) {
            onRewarpRequested();
        }
    };

    m_mixerView = std::make_unique<MixerView>(mixer, arrangement, factory, pluginHost,
        commandStack, sampleRate, maxBlockSize);
    addChildComponent(*m_mixerView);

    updateBottomPanelVisibility();
    updateCenterViewVisibility();
}

// Lays the transport bar on top, the bottom panel (if any) at the bottom, arrange view between
void MainComponent::resized() {
    auto bounds = getLocalBounds();

    m_transportBar.setBounds(bounds.removeFromTop(kTransportHeight));

    if (m_bottomPanel != BottomPanel::None) {
        auto bottomBounds = bounds.removeFromBottom(kBottomPanelHeight);

        if (m_bottomPanel == BottomPanel::PianoRoll && m_pianoRoll != nullptr) {
            m_pianoRoll->setBounds(bottomBounds);
        } else if (m_bottomPanel == BottomPanel::Mixer && m_mixerView != nullptr) {
            m_mixerView->setBounds(bottomBounds);
        }
    }

    m_trackHeaderPanel.setBounds(bounds.removeFromLeft(kTrackHeaderWidth));
    m_arrangeView.setBounds(bounds);
    m_sessionView->setBounds(bounds);
}

// Space toggles play/stop, M toggles the mixer panel, Tab toggles arrange/session view,
// Ctrl+Z / Ctrl+Shift+Z undo/redo
bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey) {
        if (m_transport.isPlaying()) {
            m_transport.stop();
        } else {
            m_transport.play();
        }
        return true;
    }

    if (key == juce::KeyPress('M')) {
        toggleMixer();
        return true;
    }

    if (key == juce::KeyPress::tabKey) {
        toggleCenterView();
        return true;
    }

    if (key == juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0)) {
        m_commandStack.undo();
        refreshAllViews();
        return true;
    }

    if (key == juce::KeyPress('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) {
        m_commandStack.redo();
        refreshAllViews();
        return true;
    }

    return false;
}

// Shows the piano roll for a clip in the bottom panel (replaces whatever is there)
void MainComponent::showPianoRollFor(std::size_t trackIndex, std::size_t placementIndex) {
    model::MidiClip& clip = m_arrangement.track(trackIndex).midiClips[placementIndex].clip;

    if (m_pianoRoll != nullptr) {
        removeChildComponent(m_pianoRoll.get());
    }

    m_pianoRoll = std::make_unique<PianoRoll>(clip, m_transport, m_sampleRate);
    addAndMakeVisible(*m_pianoRoll);

    m_bottomPanel = BottomPanel::PianoRoll;
    updateBottomPanelVisibility();
    resized();
}

// Shows the piano roll for a session slot's MIDI clip in the bottom panel
void MainComponent::showPianoRollForSession(std::size_t trackIndex, std::size_t sceneIndex) {
    model::ClipSlot& slot = m_session.slot(trackIndex, sceneIndex);
    if (slot.content != model::SlotContent::Midi) {
        return;
    }

    if (m_pianoRoll != nullptr) {
        removeChildComponent(m_pianoRoll.get());
    }

    m_pianoRoll = std::make_unique<PianoRoll>(slot.midiClip, m_transport, m_sampleRate);
    addAndMakeVisible(*m_pianoRoll);

    m_bottomPanel = BottomPanel::PianoRoll;
    updateBottomPanelVisibility();
    resized();
}

// Shows the mixer in the bottom panel, or hides the panel if the mixer is already shown
void MainComponent::toggleMixer() {
    if (m_bottomPanel == BottomPanel::Mixer) {
        m_bottomPanel = BottomPanel::None;
    } else {
        m_bottomPanel = BottomPanel::Mixer;
    }

    updateBottomPanelVisibility();
    resized();
}

// Flips the center view between the arrange view and the session view
void MainComponent::toggleCenterView() {
    m_centerView = m_centerView == CenterView::Arrange ? CenterView::Session : CenterView::Arrange;
    updateCenterViewVisibility();
}

// Repaints the arrange view, refreshes mixer strips, track headers, and the session view,
// closes any open effect editors
void MainComponent::refreshAllViews() {
    m_arrangeView.repaint();
    m_trackHeaderPanel.refreshFromModel();
    m_sessionView->refreshFromModel();

    if (m_mixerView != nullptr) {
        m_mixerView->refreshStrips();
    }
}

// juce::MenuBarModel: File, Edit, View
juce::StringArray MainComponent::getMenuBarNames() {
    return { "File", "Edit", "View" };
}

// juce::MenuBarModel: builds each top-level menu's items
juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) {
        menu.addItem(5, "New");
        menu.addItem(6, "Open...");
        menu.addItem(7, "Save");
        menu.addItem(8, "Save As...");
        menu.addItem(10, "Export Audio...");
        menu.addSeparator();
        menu.addItem(1, "Import Audio...");
    } else if (topLevelMenuIndex == 1) {
        menu.addItem(2, "Undo");
        menu.addItem(3, "Redo");
    } else if (topLevelMenuIndex == 2) {
        menu.addItem(4, "Toggle Mixer");
        menu.addItem(9, "Toggle Session View");
    }

    return menu;
}

// juce::MenuBarModel: dispatches the picked item, routing undo/redo through refreshAllViews()
void MainComponent::menuItemSelected(int menuItemID, int) {
    switch (menuItemID) {
        case 1:
            if (onImportAudioRequested) {
                onImportAudioRequested();
            }
            break;
        case 2:
            m_commandStack.undo();
            refreshAllViews();
            break;
        case 3:
            m_commandStack.redo();
            refreshAllViews();
            break;
        case 4:
            toggleMixer();
            break;
        case 5:
            if (onNewProjectRequested) {
                onNewProjectRequested();
            }
            break;
        case 6:
            if (onOpenProjectRequested) {
                onOpenProjectRequested();
            }
            break;
        case 7:
            if (onSaveProjectRequested) {
                onSaveProjectRequested();
            }
            break;
        case 8:
            if (onSaveAsProjectRequested) {
                onSaveAsProjectRequested();
            }
            break;
        case 9:
            toggleCenterView();
            break;
        case 10:
            if (onExportAudioRequested) {
                onExportAudioRequested();
            }
            break;
        default:
            break;
    }
}

// Shows only the component matching m_bottomPanel, hides the other
void MainComponent::updateBottomPanelVisibility() {
    if (m_pianoRoll != nullptr) {
        m_pianoRoll->setVisible(m_bottomPanel == BottomPanel::PianoRoll);
    }

    if (m_mixerView != nullptr) {
        m_mixerView->setVisible(m_bottomPanel == BottomPanel::Mixer);
    }
}

// Shows only the component matching m_centerView, hides the other
void MainComponent::updateCenterViewVisibility() {
    m_arrangeView.setVisible(m_centerView == CenterView::Arrange);
    m_sessionView->setVisible(m_centerView == CenterView::Session);
}

} // namespace howl::ui
