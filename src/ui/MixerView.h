// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: horizontal row of mixer strips for every track, bus, and master

#pragma once

#include "engine/EffectFactory.h"
#include "model/Arrangement.h"
#include "model/CommandStack.h"
#include "model/Mixer.h"
#include "plugins/IPluginInstance.h"
#include "ui/ChannelStripComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace howl::ui {

// Horizontal row of strips: tracks, buses, master, meters ticking at 30 Hz
class MixerView : public juce::Component, private juce::Timer {
public:
    // pluginHost may be null, the plugin section of the add-effect menu is then omitted
    MixerView(model::Mixer& mixer, model::Arrangement& arrangement,
              engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost,
              model::CommandStack& commandStack, double sampleRate, int maxBlockSize);

    // Stops the meter timer
    ~MixerView() override;

    // Lays the strips out left to right inside the viewport
    void resized() override;

    // Fills the background
    void paint(juce::Graphics& g) override;

private:
    static constexpr int kStripWidth = 96;

    // Rebuilds one ChannelStripComponent per track, bus, and master
    void rebuildStrips();

    // Pops meter readings into every strip
    void timerCallback() override;

    model::Mixer& m_mixer;
    model::Arrangement& m_arrangement;
    engine::IEffectFactory& m_factory;
    plugins::IPluginHost* m_pluginHost;
    model::CommandStack& m_commandStack;
    double m_sampleRate;
    int m_maxBlockSize;

    juce::Viewport m_viewport; // horizontal scroll when strips overflow
    juce::Component m_stripsContainer; // the viewport's viewed component, holds every strip
    juce::TextButton m_addBusButton { "Add Bus" };
    std::vector<std::unique_ptr<ChannelStripComponent>> m_strips;
};

} // namespace howl::ui
