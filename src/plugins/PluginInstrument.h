// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: wraps a hosted plugin instance as an engine::Instrument, queues MIDI per block

#pragma once

#include "engine/Instrument.h"
#include "plugins/IPluginInstance.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace howl::plugins {

class PluginInstrument : public engine::Instrument {
public:
    // Takes ownership of a loaded instance, displayName comes from the plugin's descriptor
    PluginInstrument(std::unique_ptr<IPluginInstance> instance, std::string displayName);

    // Calls release() on the instance before it is destroyed, matching PluginEffect
    ~PluginInstrument() override;

    // Forwards to the plugin's prepare, ensures the MIDI scratch buffer has headroom
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Queues a note-on at sample 0 for the next render
    void noteOn(int key, float velocity) noexcept override;

    // [RT] Queues a note-off at sample 0 for the next render
    void noteOff(int key) noexcept override;

    // [RT] Zeroes the block, renders the plugin into it, clears the queued MIDI
    void render(AudioBlock& audio) noexcept override;

    // Number of plugin params, clamped to int
    int numParameters() const override;

    // The plugin param name at index, "" when out of range
    const char* parameterName(int index) const override;

    // [RT] Forwards to setParamNormalized with the param id at index
    void setParameter(int index, float value) noexcept override;

    // Returns the last normalized value set for the param at index, its default before any set.
    // Note: changes made inside a plugin's native editor are not reflected back into this
    // cache (no host param listening yet, follow-up).
    float getParameter(int index) const noexcept;

    // Returns the descriptor name given at construction
    const char* displayName() const noexcept;

    // Returns the wrapped plugin instance, an editor window needs it for the native-editor button
    IPluginInstance& instance() noexcept;

private:
    std::unique_ptr<IPluginInstance> m_instance;
    std::string m_displayName;
    juce::MidiBuffer m_midiScratch;
    std::vector<float> m_paramValues;
};

} // namespace howl::plugins
