// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: one mixer strip, gain/pan/mute/solo/meter/FX chain, sends and routing on tracks

#include "ui/ChannelStripComponent.h"

#include "model/Commands.h"
#include "plugins/PluginEffect.h"

#include <algorithm>
#include <cmath>

namespace howl::ui {

// Stores references to the strip's dependencies and builds its controls
ChannelStripComponent::ChannelStripComponent(model::Mixer& mixer, model::StripAddress address, juce::String name,
                                              engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost,
                                              model::CommandStack& commandStack, double sampleRate, int maxBlockSize)
    : m_mixer(mixer)
    , m_address(address)
    , m_name(std::move(name))
    , m_factory(factory)
    , m_pluginHost(pluginHost)
    , m_commandStack(commandStack)
    , m_sampleRate(sampleRate)
    , m_maxBlockSize(maxBlockSize)
{
    m_gainSlider.setSliderStyle(juce::Slider::LinearVertical);
    m_gainSlider.setRange(-60.0, 12.0);
    m_gainSlider.setValue(channelStrip().gainDb(), juce::dontSendNotification);
    m_gainSlider.onValueChange = [this] {
        channelStrip().setGainDb(static_cast<float>(m_gainSlider.getValue()));
    };
    addAndMakeVisible(m_gainSlider);

    m_panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_panSlider.setRange(-1.0, 1.0);
    m_panSlider.setValue(channelStrip().pan(), juce::dontSendNotification);
    m_panSlider.onValueChange = [this] {
        channelStrip().setPan(static_cast<float>(m_panSlider.getValue()));
    };
    addAndMakeVisible(m_panSlider);

    m_muteButton.setClickingTogglesState(true);
    m_muteButton.setToggleState(channelStrip().muted(), juce::dontSendNotification);
    m_muteButton.onClick = [this] {
        channelStrip().setMuted(m_muteButton.getToggleState());
    };
    addAndMakeVisible(m_muteButton);

    m_soloButton.setClickingTogglesState(true);
    m_soloButton.setToggleState(channelStrip().soloed(), juce::dontSendNotification);
    m_soloButton.onClick = [this] {
        channelStrip().setSoloed(m_soloButton.getToggleState());
    };
    addAndMakeVisible(m_soloButton);

    m_fxList.setModel(this);
    addAndMakeVisible(m_fxList);

    m_addEffectButton.onClick = [this] {
        showAddEffectMenu();
    };
    addAndMakeVisible(m_addEffectButton);

    m_removeEffectButton.onClick = [this] {
        const int selected = m_fxList.getSelectedRow();
        if (selected < 0) {
            return;
        }

        m_commandStack.perform(std::make_unique<model::RemoveEffectCommand>(
            m_mixer, m_address, static_cast<std::size_t>(selected)));
        refreshFromModel();
    };
    addAndMakeVisible(m_removeEffectButton);

    if (m_address.kind == model::StripKind::Track) {
        m_outputCombo = std::make_unique<juce::ComboBox>();
        m_outputCombo->onChange = [this] {
            const int id = m_outputCombo->getSelectedId();
            if (id <= 0) {
                return;
            }

            if (id == 1) {
                m_mixer.setTrackOutput(m_address.index, model::Mixer::kMaster);
            } else {
                m_mixer.setTrackOutput(m_address.index, static_cast<std::size_t>(id - 2));
            }
        };
        addAndMakeVisible(*m_outputCombo);

        m_addSendButton = std::make_unique<juce::TextButton>("+S");
        m_addSendButton->onClick = [this] {
            showAddSendMenu();
        };
        addAndMakeVisible(*m_addSendButton);
    }

    refreshFromModel();
}

// Lays out the controls top to bottom
void ChannelStripComponent::resized() {
    auto bounds = getLocalBounds().reduced(4);

    bounds.removeFromTop(16); // name, drawn in paint()

    auto muteSoloArea = bounds.removeFromTop(20);
    m_muteButton.setBounds(muteSoloArea.removeFromLeft(muteSoloArea.getWidth() / 2));
    m_soloButton.setBounds(muteSoloArea);

    m_panSlider.setBounds(bounds.removeFromTop(28));

    if (m_outputCombo != nullptr) {
        m_outputCombo->setBounds(bounds.removeFromTop(20));
    }

    auto fxHeaderArea = bounds.removeFromTop(20);
    m_addEffectButton.setBounds(fxHeaderArea.removeFromLeft(fxHeaderArea.getWidth() / 2));
    m_removeEffectButton.setBounds(fxHeaderArea);

    m_fxList.setBounds(bounds.removeFromTop(80));

    if (m_addSendButton != nullptr) {
        m_addSendButton->setBounds(bounds.removeFromTop(20));

        for (auto& row : m_sendRows) {
            auto rowArea = bounds.removeFromTop(20);
            row.removeButton->setBounds(rowArea.removeFromRight(20));
            row.levelSlider->setBounds(rowArea.removeFromRight(60));
            row.label->setBounds(rowArea);
        }
    }

    m_meterBounds = bounds.removeFromRight(10);
    m_gainSlider.setBounds(bounds);
}

// Draws the name and the meter bar
void ChannelStripComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::darkgrey.darker());

    auto bounds = getLocalBounds().reduced(4);

    auto nameArea = bounds.removeFromTop(16);
    g.setColour(juce::Colours::white);
    g.drawText(m_name, nameArea, juce::Justification::centred);

    g.setColour(juce::Colours::black);
    g.fillRect(m_meterBounds);

    const int meterHeight = static_cast<int>(static_cast<float>(m_meterBounds.getHeight()) * m_meterPeak);
    g.setColour(juce::Colours::limegreen);
    g.fillRect(m_meterBounds.withTop(m_meterBounds.getBottom() - meterHeight));
}

// Pops pending meter readings and repaints the meter region, called by MixerView's timer.
// Also repaints the FX list at the same 30 Hz cadence, so a crashed row's red highlight
// shows up promptly without a dedicated timer of its own
void ChannelStripComponent::refreshMeter() {
    if (m_address.kind == model::StripKind::Track && channelStrip().muted()) {
        m_meterPeak = 0.0f;
        repaint();
        m_fxList.repaint();
        return;
    }

    engine::MeterReading reading {};
    bool gotReading = false;

    while (meterForAddress().popReading(reading)) {
        gotReading = true;
    }

    if (gotReading) {
        const float db = 20.0f * std::log10(std::max(reading.peak, 0.001f));
        m_meterPeak = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    }

    repaint();
    m_fxList.repaint();
}

// Reloads the FX list and send rows from the model, called after any structural edit
void ChannelStripComponent::refreshFromModel() {
    // Any structural edit can move or destroy effects, so close the editor first to
    // guarantee it never holds a dangling reference
    m_effectEditor.reset();

    m_fxList.updateContent();

    if (m_outputCombo != nullptr) {
        m_outputCombo->clear(juce::dontSendNotification);
        m_outputCombo->addItem("Master", 1);

        for (std::size_t i = 0; i < m_mixer.numBuses(); ++i) {
            m_outputCombo->addItem(m_mixer.busName(i), static_cast<int>(i + 2));
        }

        const std::size_t destination = m_mixer.trackOutput(m_address.index);
        if (destination == model::Mixer::kMaster) {
            m_outputCombo->setSelectedId(1, juce::dontSendNotification);
        } else {
            m_outputCombo->setSelectedId(static_cast<int>(destination + 2), juce::dontSendNotification);
        }
    }

    rebuildSendRows();
    resized();
}

// ListBoxModel, one row per effect showing displayName()
int ChannelStripComponent::getNumRows() {
    return static_cast<int>(channelStrip().effects().size());
}

// Draws one effect's display name, red with a "(crashed)" suffix for a bypassed sandbox
void ChannelStripComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {
    if (rowIsSelected) {
        g.fillAll(juce::Colours::lightblue);
    }

    auto& chain = channelStrip().effects();
    if (rowNumber < 0 || static_cast<std::size_t>(rowNumber) >= chain.size()) {
        return;
    }

    const auto effectIndex = static_cast<std::size_t>(rowNumber);
    const bool crashed = isPluginCrashed && isPluginCrashed(effectIndex);

    juce::String text = chain.at(effectIndex).displayName();
    if (crashed) {
        text << " (crashed)";
    }

    g.setColour(crashed ? juce::Colours::red : juce::Colours::black);
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

// Right click on a crashed row shows a "Restart Plugin" menu
void ChannelStripComponent::listBoxItemClicked(int row, const juce::MouseEvent& event) {
    if (!event.mods.isRightButtonDown()) {
        return;
    }

    auto& chain = channelStrip().effects();
    if (row < 0 || static_cast<std::size_t>(row) >= chain.size()) {
        return;
    }

    const auto effectIndex = static_cast<std::size_t>(row);
    if (!isPluginCrashed || !isPluginCrashed(effectIndex)) {
        return;
    }

    juce::PopupMenu menu;
    menu.addItem(1, "Restart Plugin");
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, effectIndex](int result) {
        if (result == 1 && onRestartPluginRequested) {
            onRestartPluginRequested(effectIndex);
        }
    });
}

// Opens (or replaces) the generic parameter editor for the double-clicked effect
void ChannelStripComponent::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    auto& chain = channelStrip().effects();
    if (row < 0 || static_cast<std::size_t>(row) >= chain.size()) {
        return;
    }

    engine::Effect& effect = chain.at(static_cast<std::size_t>(row));

    // Pragmatic RTTI use: only a PluginEffect can offer a native editor button
    plugins::IPluginInstance* nativeInstance = nullptr;
    if (auto* pluginEffect = dynamic_cast<plugins::PluginEffect*>(&effect)) {
        nativeInstance = &pluginEffect->instance();
    }

    m_effectEditor = std::make_unique<EffectEditorWindow>(effect, nativeInstance);

    const auto effectIndex = static_cast<std::size_t>(row);
    m_effectEditor->onMidiLearnRequested = [this, effectIndex](int paramIndex) {
        if (onMidiLearnRequested) {
            onMidiLearnRequested(effectIndex, paramIndex);
        }
    };
    m_effectEditor->onMidiUnlearnRequested = [this, effectIndex](int paramIndex) {
        if (onMidiUnlearnRequested) {
            onMidiUnlearnRequested(effectIndex, paramIndex);
        }
    };
    m_effectEditor->isParameterMapped = [this, effectIndex](int paramIndex) -> bool {
        return isParameterMapped && isParameterMapped(effectIndex, paramIndex);
    };
}

// Returns the strip this component targets
model::ChannelStrip& ChannelStripComponent::channelStrip() {
    return m_mixer.strip(m_address);
}

// Returns the meter that tracks this strip's post-fader output
engine::Meter& ChannelStripComponent::meterForAddress() {
    switch (m_address.kind) {
        case model::StripKind::Track:
            return m_mixer.trackMeter(m_address.index);
        case model::StripKind::Bus:
            return m_mixer.busMeter(m_address.index);
        case model::StripKind::Master:
        default:
            return m_mixer.masterMeter();
    }
}

// Opens the add-effect popup menu, built-ins first, then hosted plugins if a host was given
void ChannelStripComponent::showAddEffectMenu() {
    juce::PopupMenu menu;

    juce::PopupMenu builtInMenu;
    const std::vector<engine::EffectType> types = m_factory.availableTypes();
    for (std::size_t i = 0; i < types.size(); ++i) {
        builtInMenu.addItem(static_cast<int>(i + 1), m_factory.displayName(types[i]));
    }
    menu.addSubMenu("Built-in", builtInMenu);

    std::vector<plugins::PluginDescriptor> pluginList;
    if (m_pluginHost != nullptr) {
        pluginList = m_pluginHost->list();
        if (!pluginList.empty()) {
            juce::PopupMenu pluginMenu;
            for (std::size_t i = 0; i < pluginList.size(); ++i) {
                // The same plugin can appear once per format (VST3 and CLAP unified in one
                // picker), the format label is the only thing that tells those two apart
                const juce::String label = juce::String(pluginList[i].name) + " (" + pluginList[i].format + ")";
                pluginMenu.addItem(static_cast<int>(1001 + i), label);
            }
            menu.addSubMenu("Plugins", pluginMenu);
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, types, pluginList](int result) {
        if (result <= 0) {
            return;
        }

        std::unique_ptr<engine::Effect> effect;

        if (result <= static_cast<int>(types.size())) {
            effect = m_factory.create(types[static_cast<std::size_t>(result - 1)]);
        } else if (result >= 1001 && m_pluginHost != nullptr) {
            const std::size_t pluginIndex = static_cast<std::size_t>(result - 1001);
            if (pluginIndex < pluginList.size()) {
                auto instance = m_pluginHost->instantiate(pluginList[pluginIndex]);
                if (instance != nullptr) {
                    effect = std::make_unique<plugins::PluginEffect>(std::move(instance), pluginList[pluginIndex]);
                }
            }
        }

        if (effect == nullptr) {
            return;
        }

        effect->prepare(m_sampleRate, m_maxBlockSize);
        m_commandStack.perform(std::make_unique<model::AddEffectCommand>(m_mixer, m_address, std::move(effect)));
        refreshFromModel();
    });
}

// Opens the add-send popup menu, one entry per bus
void ChannelStripComponent::showAddSendMenu() {
    juce::PopupMenu menu;

    const std::size_t numBuses = m_mixer.numBuses();
    for (std::size_t i = 0; i < numBuses; ++i) {
        menu.addItem(static_cast<int>(i + 1), m_mixer.busName(i));
    }

    menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
        if (result <= 0) {
            return;
        }

        const std::size_t busIndex = static_cast<std::size_t>(result - 1);
        model::Send send { busIndex, 1.0f, false };
        m_commandStack.perform(std::make_unique<model::AddSendCommand>(m_mixer, m_address.index, send));
        refreshFromModel();
    });
}

// Rebuilds the send row child components from the model
void ChannelStripComponent::rebuildSendRows() {
    m_sendRows.clear();

    if (m_address.kind != model::StripKind::Track) {
        return;
    }

    const std::vector<model::Send>& sends = m_mixer.sends(m_address.index);

    for (std::size_t i = 0; i < sends.size(); ++i) {
        SendRow row;

        row.label = std::make_unique<juce::Label>();
        row.label->setText("-> " + juce::String(m_mixer.busName(sends[i].busIndex)), juce::dontSendNotification);
        addAndMakeVisible(*row.label);

        row.levelSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        row.levelSlider->setRange(0.0, 1.0);
        row.levelSlider->setValue(sends[i].level, juce::dontSendNotification);
        const std::size_t sendIndex = i;
        row.levelSlider->onValueChange = [this, sendIndex] {
            m_mixer.setSendLevel(m_address.index, sendIndex,
                static_cast<float>(m_sendRows[sendIndex].levelSlider->getValue()));
        };
        addAndMakeVisible(*row.levelSlider);

        row.removeButton = std::make_unique<juce::TextButton>("x");
        row.removeButton->onClick = [this, sendIndex] {
            m_commandStack.perform(std::make_unique<model::RemoveSendCommand>(m_mixer, m_address.index, sendIndex));
            refreshFromModel();
        };
        addAndMakeVisible(*row.removeButton);

        m_sendRows.push_back(std::move(row));
    }
}

} // namespace howl::ui
