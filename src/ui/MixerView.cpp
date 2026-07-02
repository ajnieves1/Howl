// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: horizontal row of mixer strips for every track, bus, and master

#include "ui/MixerView.h"

namespace howl::ui {

// Stores references, builds the viewport and strips, starts the meter timer
MixerView::MixerView(model::Mixer& mixer, model::Arrangement& arrangement,
                      engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost,
                      model::CommandStack& commandStack, double sampleRate, int maxBlockSize)
    : m_mixer(mixer)
    , m_arrangement(arrangement)
    , m_factory(factory)
    , m_pluginHost(pluginHost)
    , m_commandStack(commandStack)
    , m_sampleRate(sampleRate)
    , m_maxBlockSize(maxBlockSize)
{
    m_viewport.setViewedComponent(&m_stripsContainer, false);
    m_viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(m_viewport);

    m_addBusButton.onClick = [this] {
        m_mixer.addBus("Bus " + std::to_string(m_mixer.numBuses() + 1));
        rebuildStrips();
    };
    addAndMakeVisible(m_addBusButton);

    rebuildStrips();
    startTimerHz(30);
}

// Stops the meter timer
MixerView::~MixerView() {
    stopTimer();
}

// Lays the strips out left to right inside the viewport
void MixerView::resized() {
    auto bounds = getLocalBounds();
    m_addBusButton.setBounds(bounds.removeFromTop(24));
    m_viewport.setBounds(bounds);

    const int stripHeight = bounds.getHeight();
    m_stripsContainer.setSize(static_cast<int>(m_strips.size()) * kStripWidth, stripHeight);

    for (std::size_t i = 0; i < m_strips.size(); ++i) {
        m_strips[i]->setBounds(static_cast<int>(i) * kStripWidth, 0, kStripWidth, stripHeight);
    }
}

// Fills the background
void MixerView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::darkgrey);
}

// Rebuilds one ChannelStripComponent per track, bus, and master
void MixerView::rebuildStrips() {
    m_strips.clear();

    const std::size_t numTracks = m_arrangement.numTracks();
    for (std::size_t i = 0; i < numTracks; ++i) {
        model::StripAddress address { model::StripKind::Track, i };
        auto strip = std::make_unique<ChannelStripComponent>(m_mixer, address,
            juce::String(m_arrangement.track(i).name), m_factory, m_pluginHost,
            m_commandStack, m_sampleRate, m_maxBlockSize);
        m_stripsContainer.addAndMakeVisible(*strip);
        m_strips.push_back(std::move(strip));
    }

    const std::size_t numBuses = m_mixer.numBuses();
    for (std::size_t i = 0; i < numBuses; ++i) {
        model::StripAddress address { model::StripKind::Bus, i };
        auto strip = std::make_unique<ChannelStripComponent>(m_mixer, address,
            juce::String(m_mixer.busName(i)), m_factory, m_pluginHost,
            m_commandStack, m_sampleRate, m_maxBlockSize);
        m_stripsContainer.addAndMakeVisible(*strip);
        m_strips.push_back(std::move(strip));
    }

    model::StripAddress masterAddress { model::StripKind::Master, 0 };
    auto masterStrip = std::make_unique<ChannelStripComponent>(m_mixer, masterAddress,
        juce::String("Master"), m_factory, m_pluginHost, m_commandStack, m_sampleRate, m_maxBlockSize);
    m_stripsContainer.addAndMakeVisible(*masterStrip);
    m_strips.push_back(std::move(masterStrip));

    resized();
}

// Pops meter readings into every strip
void MixerView::timerCallback() {
    for (auto& strip : m_strips) {
        strip->refreshMeter();
    }
}

} // namespace howl::ui
