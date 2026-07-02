// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a feed-forward compressor with linked-stereo detection

#include "dsp/Compressor.h"

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
Compressor::Compressor()
    : m_paramValues {
        inverseLinear(m_thresholdDb, -60.0f, 0.0f),
        inverseExponential(m_ratio, 1.0f, 20.0f),
        inverseExponential(m_attackMs, 0.1f, 100.0f),
        inverseExponential(m_releaseMs, 10.0f, 1000.0f),
        inverseLinear(m_makeupDb, 0.0f, 24.0f)
      }
{
}

// Stores the sample rate and recomputes the envelope coefficients
void Compressor::prepare(double sampleRate, int) {
    m_sampleRate = sampleRate;
    m_envelope.prepare(sampleRate, m_attackMs, m_releaseMs);
}

// [RT] Applies feed-forward gain reduction per frame, one linked gain for all channels
void Compressor::process(AudioBlock& audio) noexcept {
    for (int frame = 0; frame < audio.numFrames; ++frame) {
        float detector = 0.0f;

        for (int channel = 0; channel < audio.numChannels; ++channel) {
            detector = std::max(detector, std::abs(audio.channels[channel][frame]));
        }

        const float env = m_envelope.processSample(detector);
        const float envDb = 20.0f * std::log10(std::max(env, 1e-9f));
        const float overDb = envDb - m_thresholdDb;
        const float grDb = overDb > 0.0f ? overDb * (1.0f / m_ratio - 1.0f) : 0.0f;
        const float gain = std::pow(10.0f, (grDb + m_makeupDb) / 20.0f);

        for (int channel = 0; channel < audio.numChannels; ++channel) {
            audio.channels[channel][frame] *= gain;
        }
    }
}

// Clears the envelope
void Compressor::reset() noexcept {
    m_envelope.reset();
}

// Returns 0, feed-forward with no lookahead
int Compressor::latencySamples() const noexcept {
    return 0;
}

// Returns 5
int Compressor::numParameters() const {
    return 5;
}

// Returns the name of the parameter at index
const char* Compressor::parameterName(int index) const {
    switch (index) {
        case kThreshold: return "Threshold";
        case kRatio: return "Ratio";
        case kAttack: return "Attack";
        case kRelease: return "Release";
        case kMakeup: return "Makeup";
        default: return "";
    }
}

// [RT] Maps and stores the value, re-prepares the envelope if the rate is known
void Compressor::setParameter(int index, float value) noexcept {
    if (index < 0 || index >= 5) {
        return;
    }

    m_paramValues[index] = clampNormalized(value);

    switch (index) {
        case kThreshold:
            m_thresholdDb = mapLinear(value, -60.0f, 0.0f);
            break;
        case kRatio:
            m_ratio = mapExponential(value, 1.0f, 20.0f);
            break;
        case kAttack:
            m_attackMs = mapExponential(value, 0.1f, 100.0f);
            break;
        case kRelease:
            m_releaseMs = mapExponential(value, 10.0f, 1000.0f);
            break;
        case kMakeup:
            m_makeupDb = mapLinear(value, 0.0f, 24.0f);
            break;
        default:
            return;
    }

    if (m_sampleRate > 0.0) {
        m_envelope.prepare(m_sampleRate, m_attackMs, m_releaseMs);
    }
}

// Returns the last normalized value set for the param at index, its default before any set
float Compressor::getParameter(int index) const noexcept {
    if (index < 0 || index >= 5) {
        return 0.0f;
    }

    return m_paramValues[index];
}

// Returns "Compressor"
const char* Compressor::displayName() const noexcept {
    return "Compressor";
}

} // namespace howl::dsp
