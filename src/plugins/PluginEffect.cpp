// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: wraps a hosted plugin instance as an engine::Effect, audio only, no MIDI

#include "plugins/PluginEffect.h"

namespace howl::plugins {

// Takes ownership of a loaded instance, displayName comes from the plugin's descriptor
PluginEffect::PluginEffect(std::unique_ptr<IPluginInstance> instance, std::string displayName)
    : m_instance(std::move(instance))
    , m_displayName(std::move(displayName))
{
}

// Calls release() on the instance before it is destroyed, matching the P2 test pattern
PluginEffect::~PluginEffect() {
    m_instance->release();
}

// Forwards to the plugin's prepare
void PluginEffect::prepare(double sampleRate, int maxBlockSize) {
    m_instance->prepare(sampleRate, maxBlockSize);

    m_paramValues.clear();
    for (const auto& param : m_instance->params()) {
        m_paramValues.push_back(param.defaultNormalized);
    }
}

// [RT] Forwards to the plugin's process with nullptr MIDI
void PluginEffect::process(AudioBlock& audio) noexcept {
    m_instance->process(audio, nullptr);
}

// IPluginInstance has no reset, no-op
void PluginEffect::reset() noexcept {
}

// Forwards the plugin's reported latency
int PluginEffect::latencySamples() const noexcept {
    return m_instance->latencySamples();
}

// Number of plugin params, clamped to int
int PluginEffect::numParameters() const {
    return static_cast<int>(m_instance->params().size());
}

// The plugin param name at index, "" when out of range
const char* PluginEffect::parameterName(int index) const {
    const auto& params = m_instance->params();

    if (index < 0 || static_cast<std::size_t>(index) >= params.size()) {
        return "";
    }

    return params[static_cast<std::size_t>(index)].name.c_str();
}

// [RT] Forwards to setParamNormalized with the param id at index
void PluginEffect::setParameter(int index, float value) noexcept {
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
float PluginEffect::getParameter(int index) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= m_paramValues.size()) {
        return 0.0f;
    }

    return m_paramValues[static_cast<std::size_t>(index)];
}

// Returns the descriptor name given at construction
const char* PluginEffect::displayName() const noexcept {
    return m_displayName.c_str();
}

// Returns the wrapped plugin instance, the editor window needs it for the native-editor button
IPluginInstance& PluginEffect::instance() noexcept {
    return *m_instance;
}

} // namespace howl::plugins
