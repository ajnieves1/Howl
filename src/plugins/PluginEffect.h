// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: wraps a hosted plugin instance as an engine::Effect, audio only, no MIDI

#pragma once

#include "engine/Effect.h"
#include "plugins/IPluginInstance.h"
#include "plugins/PluginDescriptor.h"

#include <memory>
#include <string>
#include <vector>

namespace howl::plugins {

class PluginEffect : public engine::Effect {
public:
    // Takes ownership of a loaded instance, displayName comes from the plugin's descriptor.
    // pluginFormat()/pluginPath() are empty when constructed this way (not persistable as a
    // real plugin on save/load, prefer the PluginDescriptor overload below for new code)
    PluginEffect(std::unique_ptr<IPluginInstance> instance, std::string displayName);

    // Takes ownership of a loaded instance, remembers the descriptor's name/format/path so
    // this effect can be re-instantiated by src/project on save/load
    PluginEffect(std::unique_ptr<IPluginInstance> instance, const PluginDescriptor& descriptor);

    // Calls release() on the instance before it is destroyed, matching the P2 test pattern
    ~PluginEffect() override;

    // Forwards to the plugin's prepare
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Forwards to the plugin's process with nullptr MIDI
    void process(AudioBlock& audio) noexcept override;

    // IPluginInstance has no reset, no-op
    void reset() noexcept override;

    // Forwards the plugin's reported latency
    int latencySamples() const noexcept override;

    // Number of plugin params, clamped to int
    int numParameters() const override;

    // The plugin param name at index, "" when out of range
    const char* parameterName(int index) const override;

    // [RT] Forwards to setParamNormalized with the param id at index
    void setParameter(int index, float value) noexcept override;

    // Returns the last normalized value set for the param at index, its default before any set.
    // Note: changes made inside a plugin's native editor are not reflected back into this
    // cache (no host param listening yet, follow-up).
    float getParameter(int index) const noexcept override;

    // Returns the descriptor name given at construction
    const char* displayName() const noexcept override;

    // Returns the wrapped plugin instance, the editor window needs it for the native-editor button
    IPluginInstance& instance() noexcept;

    // Returns the plugin format ("VST3"/"CLAP"), empty if constructed without a descriptor
    const std::string& pluginFormat() const noexcept;

    // Returns the plugin file path, empty if constructed without a descriptor
    const std::string& pluginPath() const noexcept;

private:
    std::unique_ptr<IPluginInstance> m_instance;
    std::string m_displayName;
    std::string m_pluginFormat;
    std::string m_pluginPath;
    std::vector<float> m_paramValues;
};

} // namespace howl::plugins
