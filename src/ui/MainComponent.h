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
#include "ui/TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <memory>

namespace howl::ui {

// The whole app shell: transport bar top, arrange view center, piano roll or mixer bottom
class MainComponent : public juce::Component {
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

    // Repaints the arrange view, refreshes mixer strips, closes any open effect editors
    void refreshAllViews();

private:
    static constexpr int kTransportHeight = 36;
    static constexpr int kBottomPanelHeight = 300;

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
    ArrangeView m_arrangeView;
    std::unique_ptr<PianoRoll> m_pianoRoll; // recreated per selected clip
    std::unique_ptr<MixerView> m_mixerView; // created once in the ctor
    BottomPanel m_bottomPanel = BottomPanel::None;
};

} // namespace howl::ui
