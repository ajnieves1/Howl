// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a one-pole peak envelope follower with separate attack and release smoothing

#pragma once

#include <cmath>

namespace howl::dsp {

class EnvelopeFollower {
public:
    // Computes the one-pole coefficients from the attack and release times
    void prepare(double sampleRate, float attackMs, float releaseMs) {
        m_attackCoeff = std::exp(-1.0f / (0.001f * attackMs * static_cast<float>(sampleRate)));
        m_releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * static_cast<float>(sampleRate)));
    }

    // [RT] Advances the envelope one step toward |input| and returns it, linear
    float processSample(float input) noexcept {
        const float x = std::abs(input);
        const float c = x > m_envelope ? m_attackCoeff : m_releaseCoeff;
        m_envelope = c * m_envelope + (1.0f - c) * x;
        return m_envelope;
    }

    // Clears the envelope to zero
    void reset() noexcept {
        m_envelope = 0.0f;
    }

private:
    float m_attackCoeff = 0.0f;
    float m_releaseCoeff = 0.0f;
    float m_envelope = 0.0f;
};

} // namespace howl::dsp
