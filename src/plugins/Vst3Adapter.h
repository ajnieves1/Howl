// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: wraps a loaded VST3 instance behind IPluginInstance

#pragma once

#include "plugins/IPluginInstance.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace howl::plugins {

class Vst3Adapter : public IPluginInstance {
public:
    // Takes ownership of an already-created plugin instance
    explicit Vst3Adapter(std::unique_ptr<juce::AudioPluginInstance> pluginInstance);

    // Closes the editor if still open
    ~Vst3Adapter() override;

    // Calls prepareToPlay and snapshots the parameter list
    void prepare(double sampleRate, int maxBlockSize) override;

    // Calls releaseResources
    void release() override;

    // [RT] midiIn must point to a const juce::MidiBuffer, may be nullptr
    void process(AudioBlock& audio, const void* midiIn) override;

    // Returns the plugin's self-reported processing latency
    int latencySamples() const noexcept override;

    // Serializes the plugin's internal state
    StateBlob saveState() const override;

    // Restores a previously serialized state
    void loadState(const StateBlob& state) override;

    // Loads a .vstpreset file into the instance, false for any other extension or on failure
    bool loadPresetFile(const juce::File& file) override;

    // Returns the snapshot taken by the last prepare() call
    const std::vector<ParamInfo>& params() const override;

    // Sets a parameter by its index into params()
    void setParamNormalized(uint32_t id, float value) override;

    bool hasEditor() const override;

    // Creates the plugin's editor and returns it as a component, nullptr when it has none
    juce::Component* openEditor() override;

    void closeEditor() override;

private:
    std::unique_ptr<juce::AudioPluginInstance> m_plugin;
    std::vector<ParamInfo> m_params;

    // Reused every block, cleared and refilled rather than rebuilt, an
    // unusually large MIDI burst can still force a reallocation inside JUCE
    juce::MidiBuffer m_scratchMidi;

    std::unique_ptr<juce::AudioProcessorEditor> m_editor;
};

} // namespace howl::plugins
