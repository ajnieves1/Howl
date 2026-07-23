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
#include "ui/BrowserResizerBar.h"
#include "ui/ChannelRackPanel.h"
#include "ui/EditTool.h"
#include "ui/FileBrowserPanel.h"
#include "ui/MixerView.h"
#include "ui/PianoRoll.h"
#include "ui/PianoRollWindow.h"
#include "ui/SessionView.h"
#include "ui/Theme.h"
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

    // Space play/stop, Tab cycles arrange/session/rack view, M mixer, B browser,
    // Ctrl+Z / Ctrl+Y undo/redo, Ctrl+N/O/S/Shift+S new/open/save/save as
    bool keyPressed(const juce::KeyPress& key) override;

    // Opens the piano roll for a clip in its own pop out window
    void showPianoRollFor(std::size_t trackIndex, std::size_t placementIndex);

    // Opens the piano roll for a session slot's MIDI clip in its own pop out window
    void showPianoRollForSession(std::size_t trackIndex, std::size_t sceneIndex);

    // Opens the piano roll for a pattern lane's MIDI clip in its own pop out window
    void showPianoRollForPattern(model::ClipAddress address);

    // Shows the mixer in the bottom panel, or hides the panel if the mixer is already shown
    void toggleMixer();

    // Shows the automation editor for a track in the bottom panel (replaces whatever is there)
    void showAutomationEditorFor(std::size_t trackIndex);

    // Cycles the center view Arrange -> Session -> Rack -> Arrange
    void toggleCenterView();

    // Shows or hides the sample browser's left column
    void toggleBrowser();

    // Sets the browser column width in pixels, clamps it, and relays out
    void setBrowserWidth(int width);

    // Sets the transport bar's metronome and count in toggles to their persisted state
    void setMetronomeEnabled(bool enabled);
    void setCountInEnabled(bool enabled);

    // Repaints the arrange view, refreshes mixer strips, track headers, and the session view,
    // closes any open effect editors
    void refreshAllViews();

    // juce::MenuBarModel: File, Edit, View, Options, Help
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // Fired after any track add/remove so the app rebuilds the audio graph and views
    std::function<void()> onTracksChanged;

    // Fired to copy the instrument from a source channel to a cloned one, app owned
    std::function<void(std::size_t, std::size_t)> onCloneInstrumentRequested;

    // Fired with the new state when the metronome or count in toggle changes
    std::function<void(bool)> onMetronomeToggled;
    std::function<void(bool)> onCountInToggled;

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

    // Fired when a file is clicked in the browser, the app previews it when it is audio
    std::function<void(juce::File)> onBrowserFileClicked;

    // Fired when the user drags the browser edge to a new width, the app persists it
    std::function<void(int)> onBrowserWidthChanged;

    // Fired with (trackIndex, file) when a sample lands on a channel rack row
    std::function<void(std::size_t, juce::File)> onSampleAssignRequested;

    // Fired with (trackIndex, file) when a preset file lands on a channel rack row
    std::function<void(std::size_t, juce::File)> onPatchDropRequested;

    // Fired with trackIndex after a step toggles on, the app previews the hit when stopped
    std::function<void(std::size_t)> onStepPreviewRequested;

    // Fired with (path, trackIndex, tick) when an audio file is dropped onto the arrange view
    std::function<void(juce::String, std::size_t, int64_t)> onAudioFileDropped;

    // Fired with (path, trackIndex, tick) when a MIDI file is dropped onto the arrange view
    std::function<void(juce::String, std::size_t, int64_t)> onMidiFileDropped;

    // Fired when File > New/Open/Save/Save As is picked
    std::function<void()> onNewProjectRequested;
    std::function<void()> onOpenProjectRequested;
    std::function<void()> onSaveProjectRequested;
    std::function<void()> onSaveAsProjectRequested;

    // Fired when File > Export Audio... is picked
    std::function<void()> onExportAudioRequested;

    // App-provided recent project file paths, newest first, feeds the Open Recent submenu
    std::function<juce::StringArray()> recentProjectFiles;

    // Fired with an absolute path when a File > Open Recent item is picked
    std::function<void(juce::String)> onOpenRecentRequested;

    // Fired when File > Audio Settings... is picked
    std::function<void()> onAudioSettingsRequested;

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
    static constexpr int kBottomPanelMinHeight = 300;
    static constexpr int kBottomPanelMaxHeight = 560;
    static constexpr int kBottomPanelTitleHeight = 22;
    static constexpr int kTrackHeaderWidth = TrackHeaderPanel::kWidth;
    static constexpr int kDefaultBrowserWidth = 220;
    static constexpr int kBrowserMinWidth = 140;
    static constexpr int kBrowserMaxWidth = 600;
    static constexpr int kBrowserResizerWidth = 6;
    static constexpr int kRecentFileMenuIdBase = 1000;

    enum class BottomPanel {
        None,
        Mixer,
        Automation
    };

    enum class CenterView {
        Arrange,
        Session,
        Rack
    };

    // Shows only the component matching m_bottomPanel, hides the other
    void updateBottomPanelVisibility();

    // The bottom panel's height: 40% of the shell, clamped so small windows keep a usable
    // panel and large windows don't drown the arrange view
    int bottomPanelHeight() const;

    // Sets the bottom panel title strip's text and shows the strip
    void setBottomPanelTitle(const juce::String& title);

    // Shows only the component matching m_centerView, hides the other
    void updateCenterViewVisibility();

    // Creates the pop out window if needed and hands it a fresh piano roll for the given title
    void openPianoRoll(std::unique_ptr<PianoRoll> roll, const juce::String& title);

    // A thin vertical line shown only while the browser edge is dragged, so the drag stays
    // cheap (one 2 px column repaints) and the shell relays out once on release
    struct ResizeGuide : public juce::Component {
        // Draws the guide as a solid selection colored line
        void paint(juce::Graphics& graphics) override {
            graphics.fillAll(theme::kSelection);
        }
    };

    model::Arrangement& m_arrangement;
    engine::Transport& m_transport;
    model::CommandStack& m_commandStack;
    model::Session& m_session;
    model::PatternBank& m_patterns;
    double m_sampleRate;
    model::SnapDivision m_snapDivision = model::SnapDivision::Step;

    // Shows a tooltip for any child component with one set, one instance covers the whole shell
    juce::TooltipWindow m_tooltipWindow { this };

    TransportBar m_transportBar;
    FileBrowserPanel m_browserPanel;
    BrowserResizerBar m_browserResizer;
    TrackHeaderPanel m_trackHeaderPanel;
    ArrangeView m_arrangeView;
    std::unique_ptr<SessionView> m_sessionView; // created once in the ctor
    std::unique_ptr<ChannelRackPanel> m_channelRackPanel; // created once in the ctor
    std::unique_ptr<PianoRollWindow> m_pianoRollWindow; // created on first open, reused after
    std::unique_ptr<MixerView> m_mixerView; // created once in the ctor
    std::unique_ptr<AutomationEditor> m_automationEditor; // recreated per requested track

    // Slim title strip above whichever bottom panel is showing, with a close button
    juce::Label m_bottomPanelTitleLabel;
    juce::TextButton m_bottomPanelCloseButton { juce::String::fromUTF8("\xC3\x97") };

    BottomPanel m_bottomPanel = BottomPanel::None;
    CenterView m_centerView = CenterView::Arrange;
    // The edit tool shared by the timeline and the piano roll
    EditTool m_editTool = EditTool::Draw;

    bool m_browserVisible = false;
    int m_browserWidth = kDefaultBrowserWidth;
    ResizeGuide m_browserResizeGuide;
};

} // namespace howl::ui
