// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: one mixer strip, gain/pan/mute/solo/meter/FX chain, sends and routing on tracks

#pragma once

#include "engine/EffectFactory.h"
#include "model/CommandStack.h"
#include "model/Mixer.h"
#include "plugins/IPluginInstance.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace howl::ui {

// v1 scope: continuous controls (fader, pan, mute, solo, send level, output
// routing) write straight to the model, not undoable. Structural edits (add
// or remove an effect or a send) go through commandStack so they can be
// undone. Only safe and only tested with the transport stopped, same
// standing caveat as ArrangeView and PianoRoll edits
class ChannelStripComponent : public juce::Component, private juce::ListBoxModel {
public:
    // Stores references to the strip's dependencies and builds its controls
    ChannelStripComponent(model::Mixer& mixer, model::StripAddress address, juce::String name,
                          engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost,
                          model::CommandStack& commandStack, double sampleRate, int maxBlockSize);

    // Lays out the controls top to bottom
    void resized() override;

    // Draws the name and the meter bar
    void paint(juce::Graphics& g) override;

    // Pops pending meter readings and repaints the meter region, called by MixerView's timer
    void refreshMeter();

    // Reloads the FX list and send rows from the model, called after any structural edit
    void refreshFromModel();

private:
    // One send row's controls: destination label, level slider, remove button
    struct SendRow {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> levelSlider;
        std::unique_ptr<juce::TextButton> removeButton;
    };

    // ListBoxModel, one row per effect showing displayName()
    int getNumRows() override;

    // Draws one effect's display name
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    // Returns the strip this component targets
    model::ChannelStrip& channelStrip();

    // Returns the meter that tracks this strip's post-fader output
    engine::Meter& meterForAddress();

    // Opens the add-effect popup menu, built-ins first, then hosted plugins if a host was given
    void showAddEffectMenu();

    // Opens the add-send popup menu, one entry per bus
    void showAddSendMenu();

    // Rebuilds the send row child components from the model
    void rebuildSendRows();

    model::Mixer& m_mixer;
    model::StripAddress m_address;
    juce::String m_name;
    engine::IEffectFactory& m_factory;
    plugins::IPluginHost* m_pluginHost;
    model::CommandStack& m_commandStack;
    double m_sampleRate;
    int m_maxBlockSize;

    juce::Slider m_gainSlider;
    juce::Slider m_panSlider;
    juce::TextButton m_muteButton { "M" };
    juce::TextButton m_soloButton { "S" };

    juce::ListBox m_fxList;
    juce::TextButton m_addEffectButton { "+" };
    juce::TextButton m_removeEffectButton { "-" };

    // Track strips only: output routing and sends
    std::unique_ptr<juce::ComboBox> m_outputCombo;
    std::unique_ptr<juce::TextButton> m_addSendButton;
    std::vector<SendRow> m_sendRows;

    float m_meterPeak = 0.0f;
};

} // namespace howl::ui
