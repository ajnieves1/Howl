// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: PluginInstrument overwrite-semantics and MIDI-queue tests over a stub IPluginInstance

#include "plugins/PluginInstrument.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

using howl::AudioBlock;
using howl::plugins::IPluginInstance;
using howl::plugins::ParamInfo;
using howl::plugins::PluginInstrument;
using howl::plugins::StateBlob;

namespace {

// Test fixture: records the block's first sample and the queued MIDI event count at process() entry
class StubPluginInstance : public IPluginInstance {
public:
    float lastObservedFirstSample = -999.0f;
    int lastMidiEventCount = -1;

    void prepare(double, int) override {
    }

    void release() override {
    }

    void process(AudioBlock& audio, const void* midiIn) override {
        lastObservedFirstSample = audio.numFrames > 0 ? audio.channels[0][0] : 0.0f;

        if (midiIn == nullptr) {
            lastMidiEventCount = -1;
            return;
        }

        const auto* buffer = static_cast<const juce::MidiBuffer*>(midiIn);
        lastMidiEventCount = 0;
        for (const auto metadata : *buffer) {
            (void)metadata;
            ++lastMidiEventCount;
        }
    }

    StateBlob saveState() const override {
        return {};
    }

    void loadState(const StateBlob&) override {
    }

    const std::vector<ParamInfo>& params() const override {
        return m_params;
    }

    void setParamNormalized(uint32_t, float) override {
    }

    bool hasEditor() const override {
        return false;
    }

    void openEditor(void*) override {
    }

    void closeEditor() override {
    }

private:
    std::vector<ParamInfo> m_params;
};

} // namespace

TEST_CASE("PluginInstrument.render zeroes the block before forwarding and passes the queued note-on", "[plugininstrument]") {
    auto stub = std::make_unique<StubPluginInstance>();
    StubPluginInstance* rawStub = stub.get();

    PluginInstrument instrument(std::move(stub), "TestSynth");
    instrument.prepare(44100.0, 512);
    instrument.noteOn(60, 1.0f);

    float samples[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 4 };

    instrument.render(block);

    REQUIRE(rawStub->lastObservedFirstSample == 0.0f);
    REQUIRE(rawStub->lastMidiEventCount == 1);
}

TEST_CASE("PluginInstrument.render clears queued MIDI after each call", "[plugininstrument]") {
    auto stub = std::make_unique<StubPluginInstance>();
    StubPluginInstance* rawStub = stub.get();

    PluginInstrument instrument(std::move(stub), "TestSynth");
    instrument.prepare(44100.0, 512);
    instrument.noteOn(60, 1.0f);

    float samples[1] = { 0.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 1 };

    instrument.render(block);
    REQUIRE(rawStub->lastMidiEventCount == 1);

    instrument.render(block);
    REQUIRE(rawStub->lastMidiEventCount == 0);
}
