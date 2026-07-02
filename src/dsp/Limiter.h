// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: an instant-attack brickwall limiter with a hard output clamp

#pragma once

#include "engine/Effect.h"

namespace howl::dsp {

class Limiter : public engine::Effect {
public:
    // Param indices, every setParameter value is normalized 0..1
    static constexpr int kCeiling = 0;
    static constexpr int kRelease = 1;

    // Stores the sample rate and recomputes the release coefficient
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Instant-attack gain reduction plus a hard clamp at the ceiling
    void process(AudioBlock& audio) noexcept override;

    // Resets the gain to unity
    void reset() noexcept override;

    // Returns 0, no lookahead
    int latencySamples() const noexcept override;

    // Returns 2
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Maps and stores the value, recomputes derived state if the rate is known
    void setParameter(int index, float value) noexcept override;

    // Returns "Limiter"
    const char* displayName() const noexcept override;

private:
    float m_ceilingDb = -0.3f;
    float m_ceilingLinear = 0.966051f;
    float m_releaseMs = 50.0f;
    float m_releaseCoeff = 0.0f;
    float m_gain = 1.0f;
    double m_sampleRate = 0.0;
};

} // namespace howl::dsp
