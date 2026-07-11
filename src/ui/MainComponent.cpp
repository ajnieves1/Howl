// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the whole app shell, transport bar top, arrange view center, piano roll or mixer bottom

#include "ui/MainComponent.h"

#include "ui/Theme.h"

namespace howl::ui {

// Builds the transport bar, arrange view, session view, and mixer, wires their callbacks
MainComponent::MainComponent(model::Arrangement& arrangement, engine::Transport& transport,
                              model::CommandStack& commandStack, model::Mixer& mixer, model::Session& session,
                              model::PatternBank& patterns, model::ArrangementNode& arrangementNode,
                              engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost, double sampleRate,
                              int maxBlockSize, const juce::File& browserRoot)
    : m_arrangement(arrangement)
    , m_transport(transport)
    , m_commandStack(commandStack)
    , m_session(session)
    , m_patterns(patterns)
    , m_sampleRate(sampleRate)
    , m_transportBar(transport, commandStack, sampleRate)
    , m_browserPanel(browserRoot)
    , m_trackHeaderPanel(arrangement, mixer, session, patterns, commandStack)
    , m_arrangeView(arrangement, transport, commandStack, patterns, sampleRate, [this] { return m_snapDivision; },
          [this] { return m_channelRackPanel->currentPatternIndex(); })
{
    // Without this, keyPressed never fires anywhere in the shell (the P5-T10 lesson)
    setWantsKeyboardFocus(true);

    addAndMakeVisible(m_transportBar);
    addChildComponent(m_browserPanel);
    addAndMakeVisible(m_trackHeaderPanel);
    addAndMakeVisible(m_arrangeView);

    m_browserPanel.onRootChanged = [this](juce::File root) {
        if (onBrowserRootChanged) {
            onBrowserRootChanged(root);
        }
    };
    m_browserPanel.onFileClicked = [this](juce::File file) {
        if (onBrowserFileClicked) {
            onBrowserFileClicked(file);
        }
    };

    m_sessionView = std::make_unique<SessionView>(session, arrangement, arrangementNode, transport, commandStack);
    addChildComponent(*m_sessionView);
    m_sessionView->onSlotEditRequested = [this](std::size_t trackIndex, std::size_t sceneIndex) {
        showPianoRollForSession(trackIndex, sceneIndex);
    };
    m_sessionView->onSessionEdited = [this] {
        refreshAllViews();
    };

    m_channelRackPanel = std::make_unique<ChannelRackPanel>(arrangement, session, patterns, commandStack);
    addChildComponent(*m_channelRackPanel);
    m_channelRackPanel->onSlotEditRequested = [this](model::ClipAddress address) {
        showPianoRollForPattern(address);
    };
    m_channelRackPanel->onSampleAssignRequested = [this](std::size_t trackIndex, juce::File file) {
        if (onSampleAssignRequested) {
            onSampleAssignRequested(trackIndex, file);
        }
    };
    m_channelRackPanel->onStepPreviewRequested = [this](std::size_t trackIndex) {
        if (onStepPreviewRequested) {
            onStepPreviewRequested(trackIndex);
        }
    };
    m_channelRackPanel->browserFileProvider = [this] {
        return m_browserPanel.selectedFile();
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
    m_trackHeaderPanel.isInstrumentCrashed = [this](std::size_t trackIndex) -> bool {
        return isInstrumentCrashed && isInstrumentCrashed(trackIndex);
    };
    m_trackHeaderPanel.onRestartInstrumentRequested = [this](std::size_t trackIndex) {
        if (onRestartInstrumentRequested) {
            onRestartInstrumentRequested(trackIndex);
        }
    };
    m_trackHeaderPanel.onAutomationRequested = [this](std::size_t trackIndex) {
        showAutomationEditorFor(trackIndex);
    };
    m_trackHeaderPanel.onTrackSelected = [this](std::ptrdiff_t trackIndex) {
        if (onTrackSelected) {
            onTrackSelected(trackIndex);
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
    m_arrangeView.browserFileProvider = [this] {
        return m_browserPanel.selectedFile();
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
    m_transportBar.onSnapChanged = [this](model::SnapDivision division) {
        m_snapDivision = division;
    };
    m_transportBar.onViewSelected = [this](int index) {
        m_centerView = static_cast<CenterView>(index);
        updateCenterViewVisibility();
    };

    m_mixerView = std::make_unique<MixerView>(mixer, arrangement, factory, pluginHost,
        commandStack, sampleRate, maxBlockSize);
    m_mixerView->onMidiLearnRequested = [this](model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex) {
        if (onMidiLearnRequested) {
            onMidiLearnRequested(stripAddress, effectIndex, paramIndex);
        }
    };
    m_mixerView->onMidiUnlearnRequested = [this](model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex) {
        if (onMidiUnlearnRequested) {
            onMidiUnlearnRequested(stripAddress, effectIndex, paramIndex);
        }
    };
    m_mixerView->isParameterMapped = [this](model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex) -> bool {
        return isParameterMapped && isParameterMapped(stripAddress, effectIndex, paramIndex);
    };
    m_mixerView->isPluginCrashed = [this](model::StripAddress stripAddress, std::size_t effectIndex) -> bool {
        return isPluginCrashed && isPluginCrashed(stripAddress, effectIndex);
    };
    m_mixerView->onRestartPluginRequested = [this](model::StripAddress stripAddress, std::size_t effectIndex) {
        if (onRestartPluginRequested) {
            onRestartPluginRequested(stripAddress, effectIndex);
        }
    };
    addChildComponent(*m_mixerView);

    m_bottomPanelTitleLabel.setColour(juce::Label::textColourId, theme::kTextPrimary);
    m_bottomPanelTitleLabel.setColour(juce::Label::backgroundColourId, theme::kPanelBg);
    addChildComponent(m_bottomPanelTitleLabel);

    m_bottomPanelCloseButton.setTooltip("Close panel");
    m_bottomPanelCloseButton.onClick = [this] {
        m_bottomPanel = BottomPanel::None;
        updateBottomPanelVisibility();
        resized();
    };
    addChildComponent(m_bottomPanelCloseButton);

    updateBottomPanelVisibility();
    updateCenterViewVisibility();
}

// Lays the transport bar on top, the bottom panel (if any) at the bottom, arrange view between
void MainComponent::resized() {
    auto bounds = getLocalBounds();

    m_transportBar.setBounds(bounds.removeFromTop(kTransportHeight));

    if (m_browserVisible) {
        m_browserPanel.setBounds(bounds.removeFromLeft(kBrowserWidth));
    }

    if (m_bottomPanel != BottomPanel::None) {
        auto bottomBounds = bounds.removeFromBottom(kBottomPanelHeight);

        auto titleStrip = bottomBounds.removeFromTop(kBottomPanelTitleHeight);
        m_bottomPanelCloseButton.setBounds(titleStrip.removeFromRight(titleStrip.getHeight()));
        m_bottomPanelTitleLabel.setBounds(titleStrip);

        if (m_bottomPanel == BottomPanel::PianoRoll && m_pianoRoll != nullptr) {
            m_pianoRoll->setBounds(bottomBounds);
        } else if (m_bottomPanel == BottomPanel::Mixer && m_mixerView != nullptr) {
            m_mixerView->setBounds(bottomBounds);
        } else if (m_bottomPanel == BottomPanel::Automation && m_automationEditor != nullptr) {
            m_automationEditor->setBounds(bottomBounds);
        }
    }

    m_trackHeaderPanel.setBounds(bounds.removeFromLeft(kTrackHeaderWidth));
    m_arrangeView.setBounds(bounds);
    m_sessionView->setBounds(bounds);
    m_channelRackPanel->setBounds(bounds);
}

// Space play/stop, Tab cycles arrange/session/rack view, M mixer, B browser,
// Ctrl+Z / Ctrl+Y undo/redo, Ctrl+N/O/S/Shift+S new/open/save/save as
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

    if (key == juce::KeyPress('B')) {
        toggleBrowser();
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

    if (key == juce::KeyPress('Y', juce::ModifierKeys::commandModifier, 0)) {
        m_commandStack.redo();
        refreshAllViews();
        return true;
    }

    if (key == juce::KeyPress('S', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) {
        if (onSaveAsProjectRequested) {
            onSaveAsProjectRequested();
        }
        return true;
    }

    if (key == juce::KeyPress('N', juce::ModifierKeys::commandModifier, 0)) {
        if (onNewProjectRequested) {
            onNewProjectRequested();
        }
        return true;
    }

    if (key == juce::KeyPress('O', juce::ModifierKeys::commandModifier, 0)) {
        if (onOpenProjectRequested) {
            onOpenProjectRequested();
        }
        return true;
    }

    if (key == juce::KeyPress('S', juce::ModifierKeys::commandModifier, 0)) {
        if (onSaveProjectRequested) {
            onSaveProjectRequested();
        }
        return true;
    }

    return false;
}

// Shows the piano roll for a clip in the bottom panel (replaces whatever is there)
void MainComponent::showPianoRollFor(std::size_t trackIndex, std::size_t placementIndex) {
    if (m_pianoRoll != nullptr) {
        removeChildComponent(m_pianoRoll.get());
    }

    const model::ClipAddress address { model::ClipAddress::Source::Arrangement, trackIndex, placementIndex };
    m_pianoRoll = std::make_unique<PianoRoll>(m_arrangement, m_session, m_patterns, address, m_commandStack,
        m_transport, m_sampleRate, [this] { return m_snapDivision; });
    addAndMakeVisible(*m_pianoRoll);

    setBottomPanelTitle("Piano Roll - " + juce::String(m_arrangement.track(trackIndex).name));
    m_bottomPanel = BottomPanel::PianoRoll;
    updateBottomPanelVisibility();
    resized();
}

// Shows the piano roll for a session slot's MIDI clip in the bottom panel
void MainComponent::showPianoRollForSession(std::size_t trackIndex, std::size_t sceneIndex) {
    if (m_session.slot(trackIndex, sceneIndex).content != model::SlotContent::Midi) {
        return;
    }

    if (m_pianoRoll != nullptr) {
        removeChildComponent(m_pianoRoll.get());
    }

    const model::ClipAddress address { model::ClipAddress::Source::Session, trackIndex, sceneIndex };
    m_pianoRoll = std::make_unique<PianoRoll>(m_arrangement, m_session, m_patterns, address, m_commandStack,
        m_transport, m_sampleRate, [this] { return m_snapDivision; });
    addAndMakeVisible(*m_pianoRoll);

    setBottomPanelTitle("Piano Roll - " + juce::String(m_arrangement.track(trackIndex).name) + " (Session)");
    m_bottomPanel = BottomPanel::PianoRoll;
    updateBottomPanelVisibility();
    resized();
}

// Shows the piano roll for a pattern lane's MIDI clip in the bottom panel
void MainComponent::showPianoRollForPattern(model::ClipAddress address) {
    if (m_pianoRoll != nullptr) {
        removeChildComponent(m_pianoRoll.get());
    }

    m_pianoRoll = std::make_unique<PianoRoll>(m_arrangement, m_session, m_patterns, address, m_commandStack,
        m_transport, m_sampleRate, [this] { return m_snapDivision; });
    addAndMakeVisible(*m_pianoRoll);

    juce::String title = "Piano Roll";
    if (address.slotIndex < m_patterns.numPatterns()) {
        title << " - " << juce::String(m_patterns.pattern(address.slotIndex).name);
    }
    if (address.trackIndex < m_arrangement.numTracks()) {
        title << " / " << juce::String(m_arrangement.track(address.trackIndex).name);
    }
    setBottomPanelTitle(title);

    m_bottomPanel = BottomPanel::PianoRoll;
    updateBottomPanelVisibility();
    resized();
}

// Shows the mixer in the bottom panel, or hides the panel if the mixer is already shown
void MainComponent::toggleMixer() {
    if (m_bottomPanel == BottomPanel::Mixer) {
        m_bottomPanel = BottomPanel::None;
    } else {
        setBottomPanelTitle("Mixer");
        m_bottomPanel = BottomPanel::Mixer;
    }

    updateBottomPanelVisibility();
    resized();
}

// Shows the automation editor for a track in the bottom panel (replaces whatever is there)
void MainComponent::showAutomationEditorFor(std::size_t trackIndex) {
    if (m_automationEditor != nullptr) {
        removeChildComponent(m_automationEditor.get());
    }

    m_automationEditor = std::make_unique<AutomationEditor>(m_arrangement, m_commandStack, trackIndex,
        [this](std::size_t index) -> std::vector<juce::String> {
            if (parameterNamesFor) {
                return parameterNamesFor(index);
            }
            return {};
        });
    addAndMakeVisible(*m_automationEditor);

    setBottomPanelTitle("Automation - " + juce::String(m_arrangement.track(trackIndex).name));
    m_bottomPanel = BottomPanel::Automation;
    updateBottomPanelVisibility();
    resized();
}

// Shows or hides the sample browser's left column
void MainComponent::toggleBrowser() {
    m_browserVisible = !m_browserVisible;
    m_browserPanel.setVisible(m_browserVisible);
    resized();
}

// Cycles the center view Arrange -> Session -> Rack -> Arrange
void MainComponent::toggleCenterView() {
    if (m_centerView == CenterView::Arrange) {
        m_centerView = CenterView::Session;
    } else if (m_centerView == CenterView::Session) {
        m_centerView = CenterView::Rack;
    } else {
        m_centerView = CenterView::Arrange;
    }

    updateCenterViewVisibility();
}

// Repaints the arrange view, refreshes mixer strips, track headers, the session view, and the
// channel rack, closes any open effect editors
void MainComponent::refreshAllViews() {
    m_arrangeView.repaint();
    m_trackHeaderPanel.refreshFromModel();
    m_sessionView->refreshFromModel();
    m_channelRackPanel->refreshFromModel();

    if (m_mixerView != nullptr) {
        m_mixerView->refreshStrips();
    }
}

// juce::MenuBarModel: File, Edit, View, Options
juce::StringArray MainComponent::getMenuBarNames() {
    return { "File", "Edit", "View", "Options" };
}

// juce::MenuBarModel: builds each top-level menu's items
juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) {
        menu.addItem(5, "New");
        menu.addItem(6, "Open...");

        juce::PopupMenu recentMenu;
        const juce::StringArray recent = recentProjectFiles ? recentProjectFiles() : juce::StringArray();
        if (recent.isEmpty()) {
            recentMenu.addItem(kRecentFileMenuIdBase - 1, "No Recent Files", false);
        } else {
            for (int i = 0; i < recent.size(); ++i) {
                recentMenu.addItem(kRecentFileMenuIdBase + i, recent[i]);
            }
        }
        menu.addSubMenu("Open Recent", recentMenu);

        menu.addItem(7, "Save");
        menu.addItem(8, "Save As...");
        menu.addItem(10, "Export Audio...");
        menu.addSeparator();
        menu.addItem(15, "Audio Settings...");
        menu.addSeparator();
        menu.addItem(1, "Import Audio...");
    } else if (topLevelMenuIndex == 1) {
        menu.addItem(2, "Undo");
        menu.addItem(3, "Redo");
    } else if (topLevelMenuIndex == 2) {
        menu.addItem(4, "Toggle Mixer");
        menu.addItem(9, "Arrange");
        menu.addItem(14, "Session");
        menu.addItem(13, "Channel Rack");
        menu.addItem(12, "Browser");
    } else if (topLevelMenuIndex == 3) {
        const bool sandboxOn = isSandboxEnabled ? isSandboxEnabled() : true;
        menu.addItem(11, "Sandbox Plugins", true, sandboxOn);
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
            m_centerView = CenterView::Arrange;
            updateCenterViewVisibility();
            break;
        case 10:
            if (onExportAudioRequested) {
                onExportAudioRequested();
            }
            break;
        case 11:
            if (onSandboxToggled) {
                const bool currentlyOn = isSandboxEnabled ? isSandboxEnabled() : true;
                onSandboxToggled(!currentlyOn);
            }
            break;
        case 12:
            toggleBrowser();
            break;
        case 13:
            m_centerView = CenterView::Rack;
            updateCenterViewVisibility();
            break;
        case 14:
            m_centerView = CenterView::Session;
            updateCenterViewVisibility();
            break;
        case 15:
            if (onAudioSettingsRequested) {
                onAudioSettingsRequested();
            }
            break;
        default:
            if (menuItemID >= kRecentFileMenuIdBase && onOpenRecentRequested && recentProjectFiles) {
                const juce::StringArray recent = recentProjectFiles();
                const int index = menuItemID - kRecentFileMenuIdBase;
                if (index >= 0 && index < recent.size()) {
                    onOpenRecentRequested(recent[index]);
                }
            }
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

    if (m_automationEditor != nullptr) {
        m_automationEditor->setVisible(m_bottomPanel == BottomPanel::Automation);
    }

    const bool showTitleStrip = m_bottomPanel != BottomPanel::None;
    m_bottomPanelTitleLabel.setVisible(showTitleStrip);
    m_bottomPanelCloseButton.setVisible(showTitleStrip);
}

// Sets the bottom panel title strip's text and shows the strip
void MainComponent::setBottomPanelTitle(const juce::String& title) {
    m_bottomPanelTitleLabel.setText(title, juce::dontSendNotification);
}

// Shows only the component matching m_centerView, hides the others, and syncs the
// transport bar's view switcher so it always reflects however the view actually changed
void MainComponent::updateCenterViewVisibility() {
    m_arrangeView.setVisible(m_centerView == CenterView::Arrange);
    m_sessionView->setVisible(m_centerView == CenterView::Session);
    m_channelRackPanel->setVisible(m_centerView == CenterView::Rack);
    m_transportBar.setActiveView(static_cast<int>(m_centerView));
}

} // namespace howl::ui
