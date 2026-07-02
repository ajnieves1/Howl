// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Mixer plugin-delay-compensation alignment tests
//
// Real-plugin PDC (a VST3/CLAP instance reporting nonzero latency through
// PluginEffect) is only fully verifiable with a latency-reporting plugin
// installed, none on CI, same caveat as the P2 plugin tests. These tests
// exercise the same compensation path with a built-in test effect instead.

#include "engine/Effect.h"
#include "model/Mixer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using howl::AudioBlock;
using howl::model::Mixer;

namespace {

// Test fixture: a real N-sample delay so PDC alignment can be observed, not just reported
class FixedLatencyEffect : public howl::engine::Effect {
public:
    explicit FixedLatencyEffect(int latency)
        : m_latency(latency)
    {
    }

    void prepare(double, int) override {
        m_lines.assign(kMaxChannels, std::vector<float>(static_cast<std::size_t>(m_latency), 0.0f));
        m_pos = 0;
    }

    void process(AudioBlock& audio) noexcept override {
        const int channels = audio.numChannels < kMaxChannels ? audio.numChannels : kMaxChannels;

        for (int frame = 0; frame < audio.numFrames; ++frame) {
            for (int channel = 0; channel < channels; ++channel) {
                const float delayed = m_lines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(m_pos)];
                m_lines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(m_pos)] = audio.channels[channel][frame];
                audio.channels[channel][frame] = delayed;
            }

            m_pos = (m_pos + 1) % m_latency;
        }
    }

    void reset() noexcept override {
        for (auto& line : m_lines) {
            std::fill(line.begin(), line.end(), 0.0f);
        }
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
        return "TestLatency";
    }

private:
    static constexpr int kMaxChannels = 8;

    int m_latency;
    std::vector<std::vector<float>> m_lines;
    int m_pos = 0;
};

} // namespace

TEST_CASE("Mixer PDC aligns two tracks to master when one carries a fixed-latency effect", "[mixer][pdc]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;
    constexpr int kLatency = 64;

    Mixer mixer;
    mixer.prepare(2, kSampleRate, kBlockSize, 1);

    auto latencyEffect = std::make_unique<FixedLatencyEffect>(kLatency);
    latencyEffect->prepare(kSampleRate, kBlockSize);
    mixer.trackStrip(0).effects().add(std::move(latencyEffect));

    mixer.updateLatencies();

    std::vector<float> track0(static_cast<std::size_t>(kBlockSize), 0.0f);
    track0[0] = 1.0f;
    float* track0Channels[1] = { track0.data() };
    AudioBlock track0Block { track0Channels, 1, kBlockSize };

    std::vector<float> track1(static_cast<std::size_t>(kBlockSize), 0.0f);
    track1[0] = 1.0f;
    float* track1Channels[1] = { track1.data() };
    AudioBlock track1Block { track1Channels, 1, kBlockSize };

    std::vector<AudioBlock> trackBuffers { track0Block, track1Block };

    std::vector<float> output(static_cast<std::size_t>(kBlockSize), 0.0f);
    float* outputChannels[1] = { output.data() };
    AudioBlock outputBlock { outputChannels, 1, kBlockSize };

    mixer.process(trackBuffers, outputBlock, 0);

    REQUIRE(output[0] == Catch::Approx(0.0f));
    REQUIRE(output[kLatency - 1] == Catch::Approx(0.0f));
    REQUIRE(output[kLatency] == Catch::Approx(2.0f));
}

TEST_CASE("Mixer PDC aligns a track routed to a latent bus with a track routed to master", "[mixer][pdc]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;
    constexpr int kLatency = 64;

    Mixer mixer;
    mixer.prepare(2, kSampleRate, kBlockSize, 1);
    const std::size_t bus = mixer.addBus("Bus A");

    auto latencyEffect = std::make_unique<FixedLatencyEffect>(kLatency);
    latencyEffect->prepare(kSampleRate, kBlockSize);
    mixer.busStrip(bus).effects().add(std::move(latencyEffect));

    mixer.setTrackOutput(0, bus);
    mixer.updateLatencies();

    std::vector<float> track0(static_cast<std::size_t>(kBlockSize), 0.0f);
    track0[0] = 1.0f;
    float* track0Channels[1] = { track0.data() };
    AudioBlock track0Block { track0Channels, 1, kBlockSize };

    std::vector<float> track1(static_cast<std::size_t>(kBlockSize), 0.0f);
    track1[0] = 1.0f;
    float* track1Channels[1] = { track1.data() };
    AudioBlock track1Block { track1Channels, 1, kBlockSize };

    std::vector<AudioBlock> trackBuffers { track0Block, track1Block };

    std::vector<float> output(static_cast<std::size_t>(kBlockSize), 0.0f);
    float* outputChannels[1] = { output.data() };
    AudioBlock outputBlock { outputChannels, 1, kBlockSize };

    mixer.process(trackBuffers, outputBlock, 0);

    REQUIRE(output[0] == Catch::Approx(0.0f));
    REQUIRE(output[kLatency - 1] == Catch::Approx(0.0f));
    REQUIRE(output[kLatency] == Catch::Approx(2.0f));
    REQUIRE(mixer.totalLatencySamples() == kLatency);
}
