// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: EffectChain and GainEffect tests

#include "dsp/GainEffect.h"
#include "engine/EffectChain.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using howl::AudioBlock;
using howl::dsp::GainEffect;
using howl::engine::EffectChain;

namespace {

// Test fixture: reports a fixed latency and otherwise passes audio through
class LatencyStubEffect : public howl::engine::Effect {
public:
    // Stores the latency this stub reports
    explicit LatencyStubEffect(int latency)
        : m_latency(latency)
    {
    }

    // No preparation needed
    void prepare(double, int) override {
    }

    // Leaves the block untouched
    void process(AudioBlock&) noexcept override {
    }

    // No internal state to clear
    void reset() noexcept override {
    }

    // Returns the fixed latency this stub was constructed with
    int latencySamples() const noexcept override {
        return m_latency;
    }

    // Exposes no parameters
    int numParameters() const override {
        return 0;
    }

    // Returns an empty name, no parameters exist
    const char* parameterName(int) const override {
        return "";
    }

    // No parameters to set
    void setParameter(int, float) noexcept override {
    }

    // No parameters exist
    float getParameter(int) const noexcept override {
        return 0.0f;
    }

    // Returns a fixed stub name
    const char* displayName() const noexcept override {
        return "LatencyStub";
    }

private:
    int m_latency;
};

} // namespace

TEST_CASE("EffectChain runs gains in series, scaling by their product", "[effectchain]") {
    float samples[4] = { 1.0f, 2.0f, -1.0f, 0.5f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 4 };

    auto first = std::make_unique<GainEffect>();
    first->setParameter(0, 1.0f);
    auto second = std::make_unique<GainEffect>();
    second->setParameter(0, 1.0f);

    EffectChain chain;
    chain.add(std::move(first));
    chain.add(std::move(second));
    chain.prepare(44100.0, 512);

    chain.process(block);

    const float expectedGain = std::pow(10.0f, 12.0f / 20.0f) * std::pow(10.0f, 12.0f / 20.0f);
    REQUIRE(samples[0] == Catch::Approx(1.0f * expectedGain));
    REQUIRE(samples[1] == Catch::Approx(2.0f * expectedGain));
    REQUIRE(samples[2] == Catch::Approx(-1.0f * expectedGain));
    REQUIRE(samples[3] == Catch::Approx(0.5f * expectedGain));
}

TEST_CASE("EffectChain latencySamples sums every effect's latency", "[effectchain]") {
    EffectChain chain;
    chain.add(std::make_unique<LatencyStubEffect>(10));
    chain.add(std::make_unique<LatencyStubEffect>(25));

    REQUIRE(chain.latencySamples() == 35);
}

TEST_CASE("EffectChain removeAt shrinks the chain", "[effectchain]") {
    EffectChain chain;
    chain.add(std::make_unique<LatencyStubEffect>(1));
    chain.add(std::make_unique<LatencyStubEffect>(2));
    chain.add(std::make_unique<LatencyStubEffect>(3));

    REQUIRE(chain.size() == 3);

    chain.removeAt(1);

    REQUIRE(chain.size() == 2);
    REQUIRE(chain.at(0).latencySamples() == 1);
    REQUIRE(chain.at(1).latencySamples() == 3);
}

TEST_CASE("EffectChain.takeAt returns the exact instance and shrinks the chain", "[effectchain]") {
    EffectChain chain;
    chain.add(std::make_unique<LatencyStubEffect>(1));
    chain.add(std::make_unique<LatencyStubEffect>(2));
    chain.add(std::make_unique<LatencyStubEffect>(3));

    howl::engine::Effect* middle = &chain.at(1);
    std::unique_ptr<howl::engine::Effect> taken = chain.takeAt(1);

    REQUIRE(taken.get() == middle);
    REQUIRE(chain.size() == 2);
    REQUIRE(chain.at(0).latencySamples() == 1);
    REQUIRE(chain.at(1).latencySamples() == 3);
}

TEST_CASE("EffectChain.insertAt restores order at the front and in the middle", "[effectchain]") {
    EffectChain chain;
    chain.add(std::make_unique<LatencyStubEffect>(1));
    chain.add(std::make_unique<LatencyStubEffect>(3));

    chain.insertAt(0, std::make_unique<LatencyStubEffect>(0));
    REQUIRE(chain.size() == 3);
    REQUIRE(chain.at(0).latencySamples() == 0);
    REQUIRE(chain.at(1).latencySamples() == 1);
    REQUIRE(chain.at(2).latencySamples() == 3);

    chain.insertAt(2, std::make_unique<LatencyStubEffect>(2));
    REQUIRE(chain.size() == 4);
    REQUIRE(chain.at(0).latencySamples() == 0);
    REQUIRE(chain.at(1).latencySamples() == 1);
    REQUIRE(chain.at(2).latencySamples() == 2);
    REQUIRE(chain.at(3).latencySamples() == 3);
}

TEST_CASE("GainEffect at unity gain leaves samples unchanged", "[effectchain]") {
    float samples[2] = { 3.0f, -3.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 2 };

    GainEffect gain;
    gain.setParameter(0, 60.0f / 72.0f);
    gain.process(block);

    REQUIRE(samples[0] == Catch::Approx(3.0f));
    REQUIRE(samples[1] == Catch::Approx(-3.0f));
}
