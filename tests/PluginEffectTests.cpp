// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: PluginEffect forwarding tests over a stub IPluginInstance

#include "plugins/PluginEffect.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>

using howl::AudioBlock;
using howl::plugins::IPluginInstance;
using howl::plugins::ParamInfo;
using howl::plugins::PluginEffect;
using howl::plugins::StateBlob;

namespace {

// Test fixture: records calls, reports a fixed latency, exposes two params
class StubPluginInstance : public IPluginInstance {
public:
    bool prepareCalled = false;
    bool processCalled = false;
    double lastSampleRate = 0.0;
    int lastMaxBlockSize = 0;
    uint32_t lastSetParamId = 0;
    float lastSetParamValue = 0.0f;
    // Points at a flag outside this object, so tests can observe release()
    // being called even after this instance has since been destroyed
    bool* releasedFlag = nullptr;

    void prepare(double sampleRate, int maxBlockSize) override {
        prepareCalled = true;
        lastSampleRate = sampleRate;
        lastMaxBlockSize = maxBlockSize;
    }

    void release() override {
        if (releasedFlag != nullptr) {
            *releasedFlag = true;
        }
    }

    void process(AudioBlock&, const void*) override {
        processCalled = true;
    }

    int latencySamples() const noexcept override {
        return 64;
    }

    StateBlob saveState() const override {
        return {};
    }

    void loadState(const StateBlob&) override {
    }

    const std::vector<ParamInfo>& params() const override {
        return m_params;
    }

    void setParamNormalized(uint32_t id, float value) override {
        lastSetParamId = id;
        lastSetParamValue = value;
    }

    bool hasEditor() const override {
        return false;
    }

    void openEditor(void*) override {
    }

    void closeEditor() override {
    }

private:
    std::vector<ParamInfo> m_params = {
        ParamInfo { 7, "Drive", 0.0f },
        ParamInfo { 9, "Mix", 1.0f }
    };
};

} // namespace

TEST_CASE("PluginEffect forwards prepare and process to the wrapped instance", "[plugineffect]") {
    auto stub = std::make_unique<StubPluginInstance>();
    StubPluginInstance* rawStub = stub.get();

    PluginEffect effect(std::move(stub), "TestSynth");
    effect.prepare(44100.0, 512);
    REQUIRE(rawStub->prepareCalled);
    REQUIRE(rawStub->lastSampleRate == 44100.0);
    REQUIRE(rawStub->lastMaxBlockSize == 512);

    float samples[1] = { 0.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 1 };
    effect.process(block);
    REQUIRE(rawStub->processCalled);
}

TEST_CASE("PluginEffect forwards latency, param count, and names", "[plugineffect]") {
    auto stub = std::make_unique<StubPluginInstance>();
    PluginEffect effect(std::move(stub), "TestSynth");

    REQUIRE(effect.latencySamples() == 64);
    REQUIRE(effect.numParameters() == 2);
    REQUIRE(std::string(effect.parameterName(1)) == "Mix");
    REQUIRE(std::string(effect.parameterName(-1)) == "");
    REQUIRE(std::string(effect.parameterName(2)) == "");
}

TEST_CASE("PluginEffect.setParameter forwards to setParamNormalized with the param id", "[plugineffect]") {
    auto stub = std::make_unique<StubPluginInstance>();
    StubPluginInstance* rawStub = stub.get();

    PluginEffect effect(std::move(stub), "TestSynth");
    effect.setParameter(1, 0.5f);

    REQUIRE(rawStub->lastSetParamId == 9);
    REQUIRE(rawStub->lastSetParamValue == 0.5f);

    effect.setParameter(-1, 0.9f);
    effect.setParameter(2, 0.9f);
    REQUIRE(rawStub->lastSetParamId == 9);
    REQUIRE(rawStub->lastSetParamValue == 0.5f);
}

TEST_CASE("PluginEffect.displayName returns the name given at construction", "[plugineffect]") {
    auto stub = std::make_unique<StubPluginInstance>();
    PluginEffect effect(std::move(stub), "TestSynth");

    REQUIRE(std::string(effect.displayName()) == "TestSynth");
}

TEST_CASE("PluginEffect calls release on the instance before it is destroyed", "[plugineffect]") {
    bool released = false;
    auto stub = std::make_unique<StubPluginInstance>();
    stub->releasedFlag = &released;

    {
        PluginEffect effect(std::move(stub), "TestSynth");
        REQUIRE_FALSE(released);
    }

    REQUIRE(released);
}
