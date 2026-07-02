// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a single-parameter gain effect, multiplies every sample

#pragma once

#include "engine/Effect.h"

namespace howl::dsp {

class GainEffect : public engine::Effect {
public:
    // Prepares the effect, gain has no rate or block size dependence
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Multiplies every sample in the block by the current linear gain
    void process(AudioBlock& audio) noexcept override;

    // No internal state to clear
    void reset() noexcept override;

    // Returns 0, this effect has no processing latency
    int latencySamples() const noexcept override;

    // Returns 1, this effect exposes only its gain
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Sets the gain, value is normalized 0..1 mapped to a dB range
    void setParameter(int index, float value) noexcept override;

    // Returns "Gain"
    const char* displayName() const noexcept override;

private:
    static constexpr int kGainParam = 0;
    static constexpr float kMinGainDb = -60.0f;
    static constexpr float kMaxGainDb = 12.0f;

    float m_gainLinear = 1.0f;
};

} // namespace howl::dsp
