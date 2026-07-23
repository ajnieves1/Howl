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

    // True when the plugin exposes the CLAP gui extension for this platform's window api
    bool hasEditor() const override;

    // Creates the plugin's gui. Returns a component the plugin parents its window into when
    // embedding is supported, or nullptr when the plugin opened its own floating window
    juce::Component* openEditor() override;

    // Hides and destroys the plugin's gui and drops the host side component
    void closeEditor() override;

private:
    struct Impl;

    // Takes ownership of a fully loaded implementation
    explicit ClapAdapter(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
    std::vector<ParamInfo> m_params;

    // The component the plugin embeds into, null for a floating gui or no gui
    std::unique_ptr<juce::Component> m_editor;

    // True once the plugin's gui has been created and still needs destroying
    bool m_guiCreated = false;
};

} // namespace howl::plugins
