// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the whole app shell, transport bar top, arrange view center, piano roll or mixer bottom

#pragma once

#include "engine/EffectFactory.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"
#include "model/CommandStack.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "model/Pattern.h"
#include "model/Session.h"
#include "plugins/IPluginInstance.h"
#include "ui/ArrangeView.h"
#include "ui/AutomationEditor.h"
#include "ui/FileBrowserPanel.h"
#include "ui/MixerView.h"
#include "ui/PianoRoll.h"
#include "ui/SessionView.h"
#include "ui/TrackHeaderPanel.h"
#include "ui/TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace howl::ui {

// The whole app shell: transport bar top, arrange view center, piano roll or mixer bottom
class MainComponent : public juce::Component, public juce::MenuBarModel, public juce::DragAndDropContainer {
public:
    MainComponent(model::Arrangement& arrangement, engine::Transport& transport,
                  model::CommandStack& commandStack, model::Mixer& mixer, model::Session& session,
                  model::PatternBank& patterns, model::ArrangementNode& arrangementNode,
                  engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost, double sampleRate,
                  int maxBlockSize, const juce::File& browserRoot);

    void resized() override;

    // Space toggles play/stop, M toggles the mixer panel, Tab toggles arrange/session view,
    // Ctrl+Z / Ctrl+Shift+Z undo/redo
    bool keyPressed(const juce::KeyPress& key) override;

    // Shows the piano roll for a clip in the bottom panel (replaces whatever is there)
    void showPianoRollFor(std::size_t trackIndex, std::size_t placementIndex);

    // Shows the piano roll for a session slot's MIDI clip in the bottom panel
    void showPianoRollForSession(std::size_t trackIndex, std::size_t sceneIndex);

    // Shows the mixer in the bottom panel, or hides the panel if the mixer is already shown
    void toggleMixer();

    // Shows the automation editor for a track in the bottom panel (replaces whatever is there)
    void showAutomationEditorFor(std::size_t trackIndex);

    // Flips the center view between the arrange view and the session view
    void toggleCenterView();

    // Shows or hides the sample browser's left column
    void toggleBrowser();

    // Repaints the arrange view, refreshes mixer strips, track headers, and the session view,
    // closes any open effect editors
    void refreshAllViews();

    // juce::MenuBarModel: File, Edit, View
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // Fired after any track add/remove so the app rebuilds the audio graph and views
    std::function<void()> onTracksChanged;

    // Fired when a MIDI track's instrument button is clicked, the app shows the picker
    std::function<void(std::size_t)> onInstrumentPickRequested;

    // Fired when a MIDI track's instrument button's "Open Editor" menu item is picked
    std::function<void(std::size_t)> onInstrumentEditRequested;

    // App-provided display name for a track's current instrument
    std::function<juce::String(std::size_t)> instrumentNameFor;

    // App-provided frozen state for a track
    std::function<bool(std::size_t)> isTrackFrozen;

    // Fired when a row's Freeze/Unfreeze Track menu item is picked, with the requested new state
    std::function<void(std::size_t, bool)> onFreezeRequested;

    // Queried with trackIndex when painting the instrument button and its right click menu
    std::function<bool(std::size_t)> isInstrumentCrashed;

    // Fired with trackIndex when a crashed instrument's "Restart Plugin" menu item is picked
    std::function<void(std::size_t)> onRestartInstrumentRequested;

    // App-provided instrument parameter names for a track, feeds the automation editor's combo
    std::function<std::vector<juce::String>(std::size_t)> parameterNamesFor;

    // Fired when a track header row is clicked, selecting it for live MIDI input, -1 for none
    std::function<void(std::ptrdiff_t)> onTrackSelected;

    // Fired when "Import Audio..." is picked, the app shows a FileChooser
    std::function<void()> onImportAudioRequested;

    // Fired when the browser's root folder changes, the app persists it
    std::function<void(juce::File)> onBrowserRootChanged;

    // Fired when a .wav file is clicked in the browser, the app starts a preview
    std::function<void(juce::File)> onBrowserFileClicked;

    // Fired with (path, trackIndex, tick) when a .wav file is dropped onto the arrange view
    std::function<void(juce::String, std::size_t, int64_t)> onAudioFileDropped;

    // Fired when File > New/Open/Save/Save As is picked
    std::function<void()> onNewProjectRequested;
    std::function<void()> onOpenProjectRequested;
    std::function<void()> onSaveProjectRequested;
    std::function<void()> onSaveAsProjectRequested;

    // Fired when File > Export Audio... is picked
    std::function<void()> onExportAudioRequested;

    // Fired after a tempo commit or an audio clip's warp toggle/BPM edit, the app rewarps clips
    std::function<void()> onRewarpRequested;

    // Fired with (stripAddress, effectIndex, paramIndex) when a parameter row's "MIDI Learn" is picked
    std::function<void(model::StripAddress, std::size_t, int)> onMidiLearnRequested;

    // Fired with (stripAddress, effectIndex, paramIndex) when "Remove MIDI Mapping" is picked
    std::function<void(model::StripAddress, std::size_t, int)> onMidiUnlearnRequested;

    // Queried with (stripAddress, effectIndex, paramIndex) when building a parameter row's menu
    std::function<bool(model::StripAddress, std::size_t, int)> isParameterMapped;

    // Queried with (stripAddress, effectIndex) when painting an FX row and its right click menu
    std::function<bool(model::StripAddress, std::size_t)> isPluginCrashed;

    // Fired with (stripAddress, effectIndex) when a crashed row's "Restart Plugin" item is picked
    std::function<void(model::StripAddress, std::size_t)> onRestartPluginRequested;

    // Queried when building the Options menu, so its Sandbox Plugins item shows a checkmark
    std::function<bool()> isSandboxEnabled;

    // Fired with the new state when Options > Sandbox Plugins is picked
    std::function<void(bool)> onSandboxToggled;

private:
    static constexpr int kTransportHeight = 36;
    static constexpr int kBottomPanelHeight = 300;
    static constexpr int kTrackHeaderWidth = TrackHeaderPanel::kWidth;
    static constexpr int kBrowserWidth = 220;

    enum class BottomPanel {
        None,
        PianoRoll,
        Mixer,
        Automation
    };

    enum class CenterView {
        Arrange,
        Session
    };

    // Shows only the component matching m_bottomPanel, hides the other
    void updateBottomPanelVisibility();

    // Shows only the component matching m_centerView, hides the other
    void updateCenterViewVisibility();

    model::Arrangement& m_arrangement;
    engine::Transport& m_transport;
    model::CommandStack& m_commandStack;
    model::Session& m_session;
    double m_sampleRate;
    model::SnapDivision m_snapDivision = model::SnapDivision::Step;

    TransportBar m_transportBar;
    FileBrowserPanel m_browserPanel;
    TrackHeaderPanel m_trackHeaderPanel;
    ArrangeView m_arrangeView;
    std::unique_ptr<SessionView> m_sessionView; // created once in the ctor
    std::unique_ptr<PianoRoll> m_pianoRoll; // recreated per selected clip
    std::unique_ptr<MixerView> m_mixerView; // created once in the ctor
    std::unique_ptr<AutomationEditor> m_automationEditor; // recreated per requested track
    BottomPanel m_bottomPanel = BottomPanel::None;
    CenterView m_centerView = CenterView::Arrange;
    bool m_browserVisible = false;
};

} // namespace howl::ui
