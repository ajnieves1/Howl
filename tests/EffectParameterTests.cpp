// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Effect::getParameter round-trip and default-value tests

#include "dsp/BuiltInEffectFactory.h"
#include "dsp/Equalizer.h"
#include "dsp/GainEffect.h"
#include "plugins/PluginEffect.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

using howl::AudioBlock;
using howl::dsp::BuiltInEffectFactory;
using howl::dsp::Equalizer;
using howl::dsp::GainEffect;
using howl::plugins::IPluginInstance;
using howl::plugins::ParamInfo;
using howl::plugins::PluginEffect;
using howl::plugins::StateBlob;

namespace {

// Test fixture: exposes two params with distinct non-zero defaults
class StubPluginInstance : public IPluginInstance {
public:
    void prepare(double, int) override {
    }

    void release() override {
    }

    void process(AudioBlock&, const void*) override {
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
    std::vector<ParamInfo> m_params = {
        ParamInfo { 3, "Drive", 0.4f },
        ParamInfo { 5, "Mix", 0.6f }
    };
};

} // namespace

TEST_CASE("Every built-in effect round-trips setParameter/getParameter for every param", "[effectparameter]") {
    BuiltInEffectFactory factory;

    for (auto type : factory.availableTypes()) {
        auto effect = factory.create(type);
        REQUIRE(effect != nullptr);

        for (int i = 0; i < effect->numParameters(); ++i) {
            effect->setParameter(i, 0.25f);
            REQUIRE(effect->getParameter(i) == Catch::Approx(0.25f));
        }

        REQUIRE(effect->getParameter(99) == 0.0f);
    }
}

TEST_CASE("GainEffect defaults to 0 dB, normalized 60/72", "[effectparameter]") {
    GainEffect gain;
    REQUIRE(gain.getParameter(0) == Catch::Approx(60.0f / 72.0f));
}

TEST_CASE("Equalizer low gain defaults to flat, normalized 0.5", "[effectparameter]") {
    Equalizer eq;
    REQUIRE(eq.getParameter(Equalizer::kLowGain) == Catch::Approx(0.5f));
}

TEST_CASE("PluginEffect.getParameter returns the plugin's defaults after prepare, and round-trips", "[effectparameter]") {
    auto stub = std::make_unique<StubPluginInstance>();
    PluginEffect effect(std::move(stub), "TestPlugin");

    effect.prepare(44100.0, 512);

    REQUIRE(effect.getParameter(0) == Catch::Approx(0.4f));
    REQUIRE(effect.getParameter(1) == Catch::Approx(0.6f));
    REQUIRE(effect.getParameter(99) == 0.0f);

    effect.setParameter(1, 0.9f);
    REQUIRE(effect.getParameter(1) == Catch::Approx(0.9f));
}
