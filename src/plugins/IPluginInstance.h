// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: the format-agnostic plugin interface and its host

#pragma once

#include "core/Types.h"
#include "plugins/PluginDescriptor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hearth::plugins {

struct ParamInfo {
    uint32_t id;
    std::string name;
    float defaultNormalized;
};

using StateBlob = std::vector<uint8_t>;

class IPluginInstance {
public:
    virtual ~IPluginInstance() = default;

    // Lifecycle, called off the audio thread
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void release() = 0;

    // [RT] Audio and MIDI processing, no allocation inside
    virtual void process(AudioBlock& audio, const void* midiIn) = 0;

    // State, off the audio thread
    virtual StateBlob saveState() const = 0;
    virtual void loadState(const StateBlob&) = 0;

    // Params, UI calls setParam, value reaches [RT] via queue or atomic, not directly
    virtual const std::vector<ParamInfo>& params() const = 0;
    virtual void setParamNormalized(uint32_t id, float value) = 0;

    // Editor, off the audio thread
    virtual bool hasEditor() const = 0;
    virtual void openEditor(void* nativeParentHandle) = 0;
    virtual void closeEditor() = 0;
};

// Loads and scans plugins of all formats, returns adapters behind IPluginInstance
class IPluginHost {
public:
    virtual ~IPluginHost() = default;

    // Scans for plugins on a background thread
    virtual void rescan() = 0;

    // Returns the cached list from the most recent scan
    virtual std::vector<PluginDescriptor> list() const = 0;

    // Instantiates the given plugin behind IPluginInstance
    virtual std::unique_ptr<IPluginInstance> instantiate(const PluginDescriptor&) = 0;
};

} // namespace hearth::plugins
