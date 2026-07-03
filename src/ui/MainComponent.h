// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the whole app shell, transport bar top, arrange view center, piano roll or mixer bottom

#pragma once

#include "engine/EffectFactory.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "plugins/IPluginInstance.h"
#include "ui/ArrangeView.h"
#include "ui/MixerView.h"
#include "ui/PianoRoll.h"
#include "ui/TrackHeaderPanel.h"
#include "ui/TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace howl::ui {

// The whole app shell: transport bar top, arrange view center, piano roll or mixer bottom
class MainComponent : public juce::Component, public juce::MenuBarModel {
public:
    MainComponent(model::Arrangement& arrangement, engine::Transport& transport,
                  model::CommandStack& commandStack, model::Mixer& mixer,
                  engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost,
                  double sampleRate, int maxBlockSize);

    void resized() override;

    // Space toggles play/stop, M toggles the mixer panel, Ctrl+Z / Ctrl+Shift+Z undo/redo
    bool keyPressed(const juce::KeyPress& key) override;

    // Shows the piano roll for a clip in the bottom panel (replaces whatever is there)
    void showPianoRollFor(std::size_t trackIndex, std::size_t placementIndex);

    // Shows the mixer in the bottom panel, or hides the panel if the mixer is already shown
    void toggleMixer();

    // Repaints the arrange view, refreshes mixer strips and track headers, closes any open effect editors
    void refreshAllViews();

    // juce::MenuBarModel: File, Edit, View
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // Fired after any track add/remove so the app rebuilds the audio graph and views
    std::function<void()> onTracksChanged;

    // Fired when a MIDI track's instrument button is clicked, the app shows the picker
    std::function<void(std::size_t)> onInstrumentPickRequested;

    // App-provided display name for a track's current instrument
    std::function<juce::String(std::size_t)> instrumentNameFor;

    // Fired when "Import Audio..." is picked, the app shows a FileChooser
    std::function<void()> onImportAudioRequested;

    // Fired with (path, trackIndex, tick) when a .wav file is dropped onto the arrange view
    std::function<void(juce::String, std::size_t, int64_t)> onAudioFileDropped;

    // Fired when File > New/Open/Save/Save As is picked
    std::function<void()> onNewProjectRequested;
    std::function<void()> onOpenProjectRequested;
    std::function<void()> onSaveProjectRequested;
    std::function<void()> onSaveAsProjectRequested;

private:
    static constexpr int kTransportHeight = 36;
    static constexpr int kBottomPanelHeight = 300;
    static constexpr int kTrackHeaderWidth = TrackHeaderPanel::kWidth;

    enum class BottomPanel {
        None,
        PianoRoll,
        Mixer
    };

    // Shows only the component matching m_bottomPanel, hides the other
    void updateBottomPanelVisibility();

    model::Arrangement& m_arrangement;
    engine::Transport& m_transport;
    model::CommandStack& m_commandStack;
    double m_sampleRate;

    TransportBar m_transportBar;
    TrackHeaderPanel m_trackHeaderPanel;
    ArrangeView m_arrangeView;
    std::unique_ptr<PianoRoll> m_pianoRoll; // recreated per selected clip
    std::unique_ptr<MixerView> m_mixerView; // created once in the ctor
    BottomPanel m_bottomPanel = BottomPanel::None;
};

} // namespace howl::ui
