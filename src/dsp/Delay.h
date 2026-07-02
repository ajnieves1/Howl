// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a feedback delay with an integer-sample ring buffer per channel

#pragma once

#include "engine/Effect.h"

#include <vector>

namespace howl::dsp {

class Delay : public engine::Effect {
public:
    // Param indices, every setParameter value is normalized 0..1
    static constexpr int kTime = 0;
    static constexpr int kFeedback = 1;
    static constexpr int kMix = 2;

    // Sizes one ring buffer per channel and recomputes the delay in samples
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Reads the wet tap, writes dry plus feedback back in, mixes dry/wet
    void process(AudioBlock& audio) noexcept override;

    // Zeroes every delay line
    void reset() noexcept override;

    // Returns 0, the dry path is never delayed
    int latencySamples() const noexcept override;

    // Returns 3
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Maps and stores the value, recomputes the delay in samples if the rate is known
    void setParameter(int index, float value) noexcept override;

    // Returns "Delay"
    const char* displayName() const noexcept override;

private:
    static constexpr int kMaxChannels = 8;
    static constexpr float kMaxDelaySeconds = 2.0f;

    // Recomputes m_delaySamples from m_timeMs and the stored sample rate
    void updateDelaySamples() noexcept;

    // One ring buffer per channel, sized only in prepare
    std::vector<std::vector<float>> m_lines;
    int m_lineSize = 0;
    int m_writePos = 0;
    int m_delaySamples = 1;
    float m_timeMs = 250.0f;
    float m_feedback = 0.35f;
    float m_mix = 0.5f;
    double m_sampleRate = 0.0;
};

} // namespace howl::dsp
