// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Mixer effects-bypass (used by track freeze) passthrough and PDC exclusion

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

// Test fixture: a real N-sample delay so bypass can be observed, not just reported
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

TEST_CASE("Mixer bypass skips a track's FX chain and drops its latency from PDC", "[mixer][bypass]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlockSize = 512;
    constexpr int kLatency = 64;

    Mixer mixer;
    mixer.prepare(1, kSampleRate, kBlockSize, 1);

    auto latencyEffect = std::make_unique<FixedLatencyEffect>(kLatency);
    latencyEffect->prepare(kSampleRate, kBlockSize);
    mixer.trackStrip(0).effects().add(std::move(latencyEffect));
    mixer.updateLatencies();

    REQUIRE(mixer.trackEffectsBypassed(0) == false);
    REQUIRE(mixer.totalLatencySamples() == kLatency);

    mixer.setTrackEffectsBypassed(0, true);
    mixer.updateLatencies();
    REQUIRE(mixer.trackEffectsBypassed(0) == true);
    REQUIRE(mixer.totalLatencySamples() == 0);

    std::vector<float> track0(static_cast<std::size_t>(kBlockSize), 0.0f);
    track0[0] = 1.0f;
    float* track0Channels[1] = { track0.data() };
    AudioBlock track0Block { track0Channels, 1, kBlockSize };
    std::vector<AudioBlock> trackBuffers { track0Block };

    std::vector<float> output(static_cast<std::size_t>(kBlockSize), 0.0f);
    float* outputChannels[1] = { output.data() };
    AudioBlock outputBlock { outputChannels, 1, kBlockSize };

    mixer.process(trackBuffers, outputBlock, 0);

    // Bypassed: the effect never ran, the impulse passes straight through undelayed
    REQUIRE(output[0] == Catch::Approx(1.0f));
    REQUIRE(output[kLatency] == Catch::Approx(0.0f));

    mixer.setTrackEffectsBypassed(0, false);
    mixer.updateLatencies();
    REQUIRE(mixer.trackEffectsBypassed(0) == false);
    REQUIRE(mixer.totalLatencySamples() == kLatency);

    std::vector<float> track0Again(static_cast<std::size_t>(kBlockSize), 0.0f);
    track0Again[0] = 1.0f;
    float* track0AgainChannels[1] = { track0Again.data() };
    AudioBlock track0AgainBlock { track0AgainChannels, 1, kBlockSize };
    std::vector<AudioBlock> trackBuffersAgain { track0AgainBlock };

    std::vector<float> output2(static_cast<std::size_t>(kBlockSize), 0.0f);
    float* output2Channels[1] = { output2.data() };
    AudioBlock output2Block { output2Channels, 1, kBlockSize };

    mixer.process(trackBuffersAgain, output2Block, 0);

    // Un-bypassed: the delay effect (never having run before) delays the impulse by its latency
    REQUIRE(output2[0] == Catch::Approx(0.0f));
    REQUIRE(output2[kLatency] == Catch::Approx(1.0f));
}
