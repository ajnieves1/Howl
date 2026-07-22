// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: loads and wraps a CLAP instance behind IPluginInstance

#pragma once

#include "plugins/IPluginInstance.h"

#include <memory>
#include <string>
#include <vector>

namespace howl::plugins {

// One CLAP plugin found on disk, a single .clap file may contain several
struct ClapPluginInfo {
    std::string path;
    std::string id;
    std::string name;
    std::string vendor;
    bool isInstrument;
};

class ClapAdapter : public IPluginInstance {
public:
    // Closes the plugin and unloads its library
    ~ClapAdapter() override;

    // Searches the standard CLAP directories and returns every plugin found
    static std::vector<ClapPluginInfo> scan();

    // Opens one .clap file directly and returns every plugin it declares, used by the
    // sandbox child to resolve a plugin id from just the path and name it was given
    static std::vector<ClapPluginInfo> scanFile(const std::string& path);

    // Loads the given CLAP plugin, returns nullptr on any failure
    static std::unique_ptr<ClapAdapter> load(const ClapPluginInfo& info);

    // Activates and starts processing at the given rate and block size
    void prepare(double sampleRate, int maxBlockSize) override;

    // Stops processing and deactivates
    void release() override;

    // [RT] midiIn must point to a const juce::MidiBuffer, may be nullptr
    void process(AudioBlock& audio, const void* midiIn) override;

    // No latencySamples() override, uses IPluginInstance's default (0);
    // the clap_plugin_latency extension is a follow-up

    // Serializes the plugin's state via the CLAP state extension
    StateBlob saveState() const override;

    // Restores a previously serialized state
    void loadState(const StateBlob& state) override;

    // Asks the plugin to load a preset file through the CLAP preset-load extension, false when
    // the plugin does not expose it or the load fails
    bool loadPresetFile(const juce::File& file) override;

    // Returns the snapshot taken by the last prepare() call
    const std::vector<ParamInfo>& params() const override;

    // Queues a normalized parameter change for the next process() call
    void setParamNormalized(uint32_t id, float value) override;

    // CLAP editor hosting is a later task, always false for now
    bool hasEditor() const override;

    // Always nullptr, CLAP GUI embedding is not implemented yet
    juce::Component* openEditor() override;

    // No-op, CLAP GUI embedding is not implemented yet
    void closeEditor() override;

private:
    struct Impl;

    // Takes ownership of a fully loaded implementation
    explicit ClapAdapter(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
    std::vector<ParamInfo> m_params;
};

} // namespace howl::plugins
