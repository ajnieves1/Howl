// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: wraps a loaded VST3 instance behind IPluginInstance

#include "plugins/Vst3Adapter.h"

#include <cstring>

namespace howl::plugins {

// Takes ownership of an already-created plugin instance
Vst3Adapter::Vst3Adapter(std::unique_ptr<juce::AudioPluginInstance> pluginInstance)
    : m_plugin(std::move(pluginInstance))
{
}

// Closes the editor if still open
Vst3Adapter::~Vst3Adapter() {
    closeEditor();
}

// Calls prepareToPlay and snapshots the parameter list
void Vst3Adapter::prepare(double sampleRate, int maxBlockSize) {
    m_plugin->setRateAndBufferSizeDetails(sampleRate, maxBlockSize);
    m_plugin->prepareToPlay(sampleRate, maxBlockSize);

    m_scratchMidi.ensureSize(2048);

    m_params.clear();
    for (auto* parameter : m_plugin->getParameters()) {
        m_params.push_back(ParamInfo {
            static_cast<uint32_t>(parameter->getParameterIndex()),
            parameter->getName(64).toStdString(),
            parameter->getDefaultValue()
        });
    }
}

// Calls releaseResources
void Vst3Adapter::release() {
    m_plugin->releaseResources();
}

// Returns the plugin's self-reported processing latency
int Vst3Adapter::latencySamples() const noexcept {
    return m_plugin->getLatencySamples();
}

// [RT] midiIn must point to a const juce::MidiBuffer, may be nullptr
void Vst3Adapter::process(AudioBlock& audio, const void* midiIn) {
    juce::AudioBuffer<float> buffer(audio.channels, audio.numChannels, audio.numFrames);

    m_scratchMidi.clear();
    if (midiIn != nullptr) {
        const auto* incoming = static_cast<const juce::MidiBuffer*>(midiIn);
        m_scratchMidi.addEvents(*incoming, 0, audio.numFrames, 0);
    }

    m_plugin->processBlock(buffer, m_scratchMidi);
}

// Serializes the plugin's internal state
StateBlob Vst3Adapter::saveState() const {
    juce::MemoryBlock block;

    // getStateInformation() is not const-qualified in JUCE, but only
    // serializes existing state, it does not mutate the plugin
    const_cast<juce::AudioPluginInstance*>(m_plugin.get())->getStateInformation(block);

    StateBlob blob(block.getSize());
    std::memcpy(blob.data(), block.getData(), block.getSize());
    return blob;
}

// Restores a previously serialized state
void Vst3Adapter::loadState(const StateBlob& state) {
    m_plugin->setStateInformation(state.data(), static_cast<int>(state.size()));
}

// Loads a .vstpreset file into the instance through JUCE's VST3 preset extension
bool Vst3Adapter::loadPresetFile(const juce::File& file) {
    if (!file.hasFileExtension("vstpreset")) {
        return false;
    }

    juce::MemoryBlock block;
    if (!file.loadFileAsData(block)) {
        return false;
    }

    // Visits the VST3 client extension of the hosted instance and feeds it the preset bytes
    struct PresetSetter : juce::ExtensionsVisitor {
        // Stores the preset data to hand to the client
        explicit PresetSetter(const juce::MemoryBlock& presetData)
            : data(presetData) {}

        // Loads the preset into the VST3 client, recording whether it took
        void visitVST3Client(const VST3Client& client) override {
            ok = client.setPreset(data);
        }

        const juce::MemoryBlock& data;
        bool ok = false;
    };

    PresetSetter setter(block);
    m_plugin->getExtensions(setter);
    return setter.ok;
}

// Returns the snapshot taken by the last prepare() call
const std::vector<ParamInfo>& Vst3Adapter::params() const {
    return m_params;
}

// Sets a parameter by its index into params()
void Vst3Adapter::setParamNormalized(uint32_t id, float value) {
    auto& parameters = m_plugin->getParameters();
    const int index = static_cast<int>(id);
    if (index >= 0 && index < parameters.size()) {
        parameters[index]->setValue(value);
    }
}

bool Vst3Adapter::hasEditor() const {
    return m_plugin->hasEditor();
}

// Creates the plugin's editor and returns it as a component, nullptr when it has none
juce::Component* Vst3Adapter::openEditor() {
    if (m_editor == nullptr) {
        m_editor.reset(m_plugin->createEditorIfNeeded());
    }
    return m_editor.get();
}

void Vst3Adapter::closeEditor() {
    m_editor.reset();
}

} // namespace howl::plugins
