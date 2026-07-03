// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: wraps a hosted plugin instance as an engine::Instrument, queues MIDI per block

#include "plugins/PluginInstrument.h"

namespace howl::plugins {

// Takes ownership of a loaded instance, displayName comes from the plugin's descriptor.
// pluginFormat()/pluginPath() are empty when constructed this way (not persistable as a
// real plugin on save/load, prefer the PluginDescriptor overload below for new code)
PluginInstrument::PluginInstrument(std::unique_ptr<IPluginInstance> instance, std::string displayName)
    : m_instance(std::move(instance))
    , m_displayName(std::move(displayName))
{
}

// Takes ownership of a loaded instance, remembers the descriptor's name/format/path so
// this instrument can be re-instantiated by src/project on save/load
PluginInstrument::PluginInstrument(std::unique_ptr<IPluginInstance> instance, const PluginDescriptor& descriptor)
    : m_instance(std::move(instance))
    , m_displayName(descriptor.name)
    , m_pluginFormat(descriptor.format)
    , m_pluginPath(descriptor.path)
{
}

// Calls release() on the instance before it is destroyed, matching PluginEffect
PluginInstrument::~PluginInstrument() {
    m_instance->release();
}

// Forwards to the plugin's prepare, ensures the MIDI scratch buffer has headroom
void PluginInstrument::prepare(double sampleRate, int maxBlockSize) {
    m_instance->prepare(sampleRate, maxBlockSize);
    m_midiScratch.ensureSize(2048);

    m_paramValues.clear();
    for (const auto& param : m_instance->params()) {
        m_paramValues.push_back(param.defaultNormalized);
    }
}

// [RT] Queues a note-on at sample 0 for the next render
void PluginInstrument::noteOn(int key, float velocity) noexcept {
    m_midiScratch.addEvent(juce::MidiMessage::noteOn(1, key, velocity), 0);
}

// [RT] Queues a note-off at sample 0 for the next render
void PluginInstrument::noteOff(int key) noexcept {
    m_midiScratch.addEvent(juce::MidiMessage::noteOff(1, key), 0);
}

// [RT] Zeroes the block, renders the plugin into it, clears the queued MIDI
void PluginInstrument::render(AudioBlock& audio) noexcept {
    for (int channel = 0; channel < audio.numChannels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            audio.channels[channel][frame] = 0.0f;
        }
    }

    m_instance->process(audio, &m_midiScratch);
    m_midiScratch.clear();
}

// Number of plugin params, clamped to int
int PluginInstrument::numParameters() const {
    return static_cast<int>(m_instance->params().size());
}

// The plugin param name at index, "" when out of range
const char* PluginInstrument::parameterName(int index) const {
    const auto& params = m_instance->params();

    if (index < 0 || static_cast<std::size_t>(index) >= params.size()) {
        return "";
    }

    return params[static_cast<std::size_t>(index)].name.c_str();
}

// [RT] Forwards to setParamNormalized with the param id at index
void PluginInstrument::setParameter(int index, float value) noexcept {
    const auto& params = m_instance->params();

    if (index < 0 || static_cast<std::size_t>(index) >= params.size()) {
        return;
    }

    if (static_cast<std::size_t>(index) < m_paramValues.size()) {
        m_paramValues[static_cast<std::size_t>(index)] = value;
    }

    m_instance->setParamNormalized(params[static_cast<std::size_t>(index)].id, value);
}

// Returns the last normalized value set for the param at index, its default before any set.
// Note: changes made inside a plugin's native editor are not reflected back into this
// cache (no host param listening yet, follow-up).
float PluginInstrument::getParameter(int index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= m_paramValues.size()) {
        return 0.0f;
    }

    return m_paramValues[static_cast<std::size_t>(index)];
}

// Returns the descriptor name given at construction
const char* PluginInstrument::displayName() const noexcept {
    return m_displayName.c_str();
}

// Returns the wrapped plugin instance, an editor window needs it for the native-editor button
IPluginInstance& PluginInstrument::instance() noexcept {
    return *m_instance;
}

// Returns the plugin format ("VST3"/"CLAP"), empty if constructed without a descriptor
const std::string& PluginInstrument::pluginFormat() const noexcept {
    return m_pluginFormat;
}

// Returns the plugin file path, empty if constructed without a descriptor
const std::string& PluginInstrument::pluginPath() const noexcept {
    return m_pluginPath;
}

} // namespace howl::plugins
