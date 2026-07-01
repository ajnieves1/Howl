// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: wraps a loaded VST3 instance behind IPluginInstance

#include "plugins/Vst3Adapter.h"

#include <cstring>

namespace hearth::plugins {

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

// Embeds the plugin's native editor under nativeParentHandle
void Vst3Adapter::openEditor(void* nativeParentHandle) {
    if (m_editor != nullptr) {
        return;
    }

    m_editor.reset(m_plugin->createEditorIfNeeded());
    if (m_editor != nullptr) {
        m_editor->addToDesktop(0, nativeParentHandle);
        m_editor->setVisible(true);
    }
}

void Vst3Adapter::closeEditor() {
    if (m_editor != nullptr) {
        m_editor->removeFromDesktop();
        m_editor.reset();
    }
}

} // namespace hearth::plugins
