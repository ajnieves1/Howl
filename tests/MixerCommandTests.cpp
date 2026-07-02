// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: mixer effect and send commands, pure model, no JUCE

#include "engine/Effect.h"
#include "model/Commands.h"
#include "model/Mixer.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using howl::AudioBlock;
using howl::model::AddEffectCommand;
using howl::model::AddSendCommand;
using howl::model::Mixer;
using howl::model::RemoveEffectCommand;
using howl::model::RemoveSendCommand;
using howl::model::Send;
using howl::model::StripAddress;
using howl::model::StripKind;

namespace {

// Test fixture: reports a fixed latency, otherwise passes audio through untouched
class LatencyOnlyEffect : public howl::engine::Effect {
public:
    explicit LatencyOnlyEffect(int latency)
        : m_latency(latency)
    {
    }

    void prepare(double, int) override {
    }

    void process(AudioBlock&) noexcept override {
    }

    void reset() noexcept override {
    }

    int latencySamples() const noexcept override {
        return m_latency;
    }

    int numParameters() const override {
        return 0;
    }

    const char* parameterName(int) const override {
        return "";
    }

    void setParameter(int, float) noexcept override {
    }

    // No parameters exist
    float getParameter(int) const noexcept override {
        return 0.0f;
    }

    const char* displayName() const noexcept override {
        return "LatencyOnly";
    }

private:
    int m_latency;
};

} // namespace

TEST_CASE("AddEffectCommand execute grows a track strip's chain and undo shrinks it", "[mixercommand]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);

    StripAddress trackAddress { StripKind::Track, 0 };
    AddEffectCommand command(mixer, trackAddress, std::make_unique<LatencyOnlyEffect>(5));

    REQUIRE(mixer.strip(trackAddress).effects().size() == 0);
    command.execute();
    REQUIRE(mixer.strip(trackAddress).effects().size() == 1);
    command.undo();
    REQUIRE(mixer.strip(trackAddress).effects().size() == 0);
}

TEST_CASE("AddEffectCommand execute grows the master strip's chain and undo shrinks it", "[mixercommand]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);

    StripAddress masterAddress { StripKind::Master, 0 };
    AddEffectCommand command(mixer, masterAddress, std::make_unique<LatencyOnlyEffect>(5));

    REQUIRE(mixer.strip(masterAddress).effects().size() == 0);
    command.execute();
    REQUIRE(mixer.strip(masterAddress).effects().size() == 1);
    command.undo();
    REQUIRE(mixer.strip(masterAddress).effects().size() == 0);
}

TEST_CASE("RemoveEffectCommand undo restores the exact instance at the same index", "[mixercommand]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);

    StripAddress trackAddress { StripKind::Track, 0 };
    auto& chain = mixer.strip(trackAddress).effects();
    chain.add(std::make_unique<LatencyOnlyEffect>(1));
    chain.add(std::make_unique<LatencyOnlyEffect>(2));
    chain.add(std::make_unique<LatencyOnlyEffect>(3));

    howl::engine::Effect* middle = &chain.at(1);

    RemoveEffectCommand command(mixer, trackAddress, 1);
    command.execute();
    REQUIRE(chain.size() == 2);

    command.undo();
    REQUIRE(chain.size() == 3);
    REQUIRE(&chain.at(1) == middle);
}

TEST_CASE("Adding and undoing an effect recomputes PDC latencies", "[mixercommand]") {
    Mixer mixer;
    mixer.prepare(2, 44100.0, 512, 1);

    const int baseline = mixer.totalLatencySamples();

    StripAddress trackAddress { StripKind::Track, 0 };
    AddEffectCommand command(mixer, trackAddress, std::make_unique<LatencyOnlyEffect>(64));

    command.execute();
    REQUIRE(mixer.totalLatencySamples() == baseline + 64);

    command.undo();
    REQUIRE(mixer.totalLatencySamples() == baseline);
}

TEST_CASE("AddSendCommand and RemoveSendCommand round-trip a track's sends", "[mixercommand]") {
    Mixer mixer;
    mixer.prepare(1, 44100.0, 512, 1);
    const std::size_t bus = mixer.addBus("Bus A");

    Send send { bus, 0.75f, false };
    AddSendCommand addCommand(mixer, 0, send);

    REQUIRE(mixer.sends(0).size() == 0);
    addCommand.execute();
    REQUIRE(mixer.sends(0).size() == 1);
    REQUIRE(mixer.sends(0)[0].busIndex == bus);
    REQUIRE(mixer.sends(0)[0].level == 0.75f);

    addCommand.undo();
    REQUIRE(mixer.sends(0).size() == 0);

    addCommand.execute();
    RemoveSendCommand removeCommand(mixer, 0, 0);
    removeCommand.execute();
    REQUIRE(mixer.sends(0).size() == 0);

    removeCommand.undo();
    REQUIRE(mixer.sends(0).size() == 1);
    REQUIRE(mixer.sends(0)[0].busIndex == bus);
    REQUIRE(mixer.sends(0)[0].level == 0.75f);
}
