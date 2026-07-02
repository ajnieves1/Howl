// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: an instant-attack brickwall limiter with a hard output clamp

#include "dsp/Limiter.h"

#include <algorithm>
#include <cmath>

namespace howl::dsp {

namespace {

// Clamps a normalized value to 0..1
float clampNormalized(float value) noexcept {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

// Maps a normalized 0..1 value linearly onto [min, max]
float mapLinear(float value, float min, float max) noexcept {
    return min + clampNormalized(value) * (max - min);
}

// Maps a normalized 0..1 value exponentially onto [min, max]
float mapExponential(float value, float min, float max) noexcept {
    return min * std::pow(max / min, clampNormalized(value));
}

// Converts a real-unit value back to the normalized 0..1 value a linear map would produce
float inverseLinear(float x, float min, float max) noexcept {
    return (x - min) / (max - min);
}

// Converts a real-unit value back to the normalized 0..1 value an exponential map would produce
float inverseExponential(float x, float min, float max) noexcept {
    return std::log(x / min) / std::log(max / min);
}

} // namespace

// Initializes the normalized parameter cache from the real-unit defaults
Limiter::Limiter()
    : m_paramValues {
        inverseLinear(m_ceilingDb, -24.0f, 0.0f),
        inverseExponential(m_releaseMs, 10.0f, 1000.0f)
      }
{
}

// Stores the sample rate and recomputes the release coefficient
void Limiter::prepare(double sampleRate, int) {
    m_sampleRate = sampleRate;
    m_releaseCoeff = std::exp(-1.0f / (0.001f * m_releaseMs * static_cast<float>(sampleRate)));
    m_ceilingLinear = std::pow(10.0f, m_ceilingDb / 20.0f);
}

// [RT] Instant-attack gain reduction plus a hard clamp at the ceiling
void Limiter::process(AudioBlock& audio) noexcept {
    for (int frame = 0; frame < audio.numFrames; ++frame) {
        float peak = 0.0f;

        for (int channel = 0; channel < audio.numChannels; ++channel) {
            peak = std::max(peak, std::abs(audio.channels[channel][frame]));
        }

        if (peak > 0.0f && peak * m_gain > m_ceilingLinear) {
            m_gain = m_ceilingLinear / peak;
        } else {
            m_gain = 1.0f + m_releaseCoeff * (m_gain - 1.0f);
        }

        for (int channel = 0; channel < audio.numChannels; ++channel) {
            float sample = audio.channels[channel][frame] * m_gain;
            sample = std::max(-m_ceilingLinear, std::min(m_ceilingLinear, sample));
            audio.channels[channel][frame] = sample;
        }
    }
}

// Resets the gain to unity
void Limiter::reset() noexcept {
    m_gain = 1.0f;
}

// Returns 0, no lookahead
int Limiter::latencySamples() const noexcept {
    return 0;
}

// Returns 2
int Limiter::numParameters() const {
    return 2;
}

// Returns the name of the parameter at index
const char* Limiter::parameterName(int index) const {
    switch (index) {
        case kCeiling: return "Ceiling";
        case kRelease: return "Release";
        default: return "";
    }
}

// [RT] Maps and stores the value, recomputes derived state if the rate is known
void Limiter::setParameter(int index, float value) noexcept {
    if (index < 0 || index >= 2) {
        return;
    }

    m_paramValues[index] = clampNormalized(value);

    switch (index) {
        case kCeiling:
            m_ceilingDb = mapLinear(value, -24.0f, 0.0f);
            m_ceilingLinear = std::pow(10.0f, m_ceilingDb / 20.0f);
            break;
        case kRelease:
            m_releaseMs = mapExponential(value, 10.0f, 1000.0f);
            if (m_sampleRate > 0.0) {
                m_releaseCoeff = std::exp(-1.0f / (0.001f * m_releaseMs * static_cast<float>(m_sampleRate)));
            }
            break;
        default:
            break;
    }
}

// Returns the last normalized value set for the param at index, its default before any set
float Limiter::getParameter(int index) const noexcept {
    if (index < 0 || index >= 2) {
        return 0.0f;
    }

    return m_paramValues[index];
}

// Returns "Limiter"
const char* Limiter::displayName() const noexcept {
    return "Limiter";
}

} // namespace howl::dsp
