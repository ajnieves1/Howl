// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a fixed three-band parametric EQ (low shelf, peak, high shelf)

#include "dsp/Equalizer.h"

#include <cmath>

namespace howl::dsp {

namespace {

constexpr float kPi = 3.14159265358979323846f;

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

// RBJ Audio EQ Cookbook low shelf, slope S = 1
BiquadCoeffs computeLowShelf(float freqHz, float gainDb, double sampleRate) noexcept {
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * freqHz / static_cast<float>(sampleRate);
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / std::sqrt(2.0f);
    const float sqrtA = std::sqrt(a);

    const float b0 = a * ((a + 1.0f) - (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha);
    const float b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosW0);
    const float b2 = a * ((a + 1.0f) - (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha);
    const float a0 = (a + 1.0f) + (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
    const float a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosW0);
    const float a2 = (a + 1.0f) + (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha;

    return BiquadCoeffs { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// RBJ Audio EQ Cookbook high shelf, slope S = 1
BiquadCoeffs computeHighShelf(float freqHz, float gainDb, double sampleRate) noexcept {
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * freqHz / static_cast<float>(sampleRate);
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / std::sqrt(2.0f);
    const float sqrtA = std::sqrt(a);

    const float b0 = a * ((a + 1.0f) + (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha);
    const float b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosW0);
    const float b2 = a * ((a + 1.0f) + (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha);
    const float a0 = (a + 1.0f) - (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
    const float a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosW0);
    const float a2 = (a + 1.0f) - (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha;

    return BiquadCoeffs { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// RBJ Audio EQ Cookbook peaking EQ
BiquadCoeffs computePeaking(float freqHz, float gainDb, float q, double sampleRate) noexcept {
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * freqHz / static_cast<float>(sampleRate);
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * q);

    const float b0 = 1.0f + alpha * a;
    const float b1 = -2.0f * cosW0;
    const float b2 = 1.0f - alpha * a;
    const float a0 = 1.0f + alpha / a;
    const float a1 = -2.0f * cosW0;
    const float a2 = 1.0f - alpha / a;

    return BiquadCoeffs { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

} // namespace

// Stores the sample rate and recomputes every band's coefficients
void Equalizer::prepare(double sampleRate, int) {
    m_sampleRate = sampleRate;
    updateBand(0);
    updateBand(1);
    updateBand(2);
}

// [RT] Runs the three bands in series, per channel, direct form II transposed
void Equalizer::process(AudioBlock& audio) noexcept {
    const BiquadCoeffs c0 = m_coeffs[0][m_active[0].load(std::memory_order_acquire)];
    const BiquadCoeffs c1 = m_coeffs[1][m_active[1].load(std::memory_order_acquire)];
    const BiquadCoeffs c2 = m_coeffs[2][m_active[2].load(std::memory_order_acquire)];

    const int channels = audio.numChannels < kMaxChannels ? audio.numChannels : kMaxChannels;

    for (int channel = 0; channel < channels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            float x = audio.channels[channel][frame];

            float y = c0.b0 * x + m_state[0][channel][0];
            m_state[0][channel][0] = c0.b1 * x - c0.a1 * y + m_state[0][channel][1];
            m_state[0][channel][1] = c0.b2 * x - c0.a2 * y;
            x = y;

            y = c1.b0 * x + m_state[1][channel][0];
            m_state[1][channel][0] = c1.b1 * x - c1.a1 * y + m_state[1][channel][1];
            m_state[1][channel][1] = c1.b2 * x - c1.a2 * y;
            x = y;

            y = c2.b0 * x + m_state[2][channel][0];
            m_state[2][channel][0] = c2.b1 * x - c2.a1 * y + m_state[2][channel][1];
            m_state[2][channel][1] = c2.b2 * x - c2.a2 * y;

            audio.channels[channel][frame] = y;
        }
    }
}

// Zeroes the filter state, coefficients are untouched
void Equalizer::reset() noexcept {
    for (int band = 0; band < kNumBands; ++band) {
        for (int channel = 0; channel < kMaxChannels; ++channel) {
            m_state[band][channel][0] = 0.0f;
            m_state[band][channel][1] = 0.0f;
        }
    }
}

// Returns 0, IIR filters add no block latency
int Equalizer::latencySamples() const noexcept {
    return 0;
}

// Returns 7
int Equalizer::numParameters() const {
    return 7;
}

// Returns the name of the parameter at index
const char* Equalizer::parameterName(int index) const {
    switch (index) {
        case kLowFreq: return "Low Freq";
        case kLowGain: return "Low Gain";
        case kPeakFreq: return "Peak Freq";
        case kPeakGain: return "Peak Gain";
        case kPeakQ: return "Peak Q";
        case kHighFreq: return "High Freq";
        case kHighGain: return "High Gain";
        default: return "";
    }
}

// [RT] Maps and stores the value, then republishes the affected band's coefficients
void Equalizer::setParameter(int index, float value) noexcept {
    switch (index) {
        case kLowFreq:
            m_lowFreqHz = mapExponential(value, 20.0f, 2000.0f);
            if (m_sampleRate > 0.0) {
                updateBand(0);
            }
            break;
        case kLowGain:
            m_lowGainDb = mapLinear(value, -24.0f, 24.0f);
            if (m_sampleRate > 0.0) {
                updateBand(0);
            }
            break;
        case kPeakFreq:
            m_peakFreqHz = mapExponential(value, 20.0f, 20000.0f);
            if (m_sampleRate > 0.0) {
                updateBand(1);
            }
            break;
        case kPeakGain:
            m_peakGainDb = mapLinear(value, -24.0f, 24.0f);
            if (m_sampleRate > 0.0) {
                updateBand(1);
            }
            break;
        case kPeakQ:
            m_peakQ = mapExponential(value, 0.3f, 10.0f);
            if (m_sampleRate > 0.0) {
                updateBand(1);
            }
            break;
        case kHighFreq:
            m_highFreqHz = mapExponential(value, 200.0f, 20000.0f);
            if (m_sampleRate > 0.0) {
                updateBand(2);
            }
            break;
        case kHighGain:
            m_highGainDb = mapLinear(value, -24.0f, 24.0f);
            if (m_sampleRate > 0.0) {
                updateBand(2);
            }
            break;
        default:
            break;
    }
}

// Returns "EQ"
const char* Equalizer::displayName() const noexcept {
    return "EQ";
}

// Recomputes one band's coefficients into its inactive slot and publishes it
void Equalizer::updateBand(int band) noexcept {
    const int inactive = 1 - m_active[band].load(std::memory_order_relaxed);

    switch (band) {
        case 0:
            m_coeffs[0][inactive] = computeLowShelf(m_lowFreqHz, m_lowGainDb, m_sampleRate);
            break;
        case 1:
            m_coeffs[1][inactive] = computePeaking(m_peakFreqHz, m_peakGainDb, m_peakQ, m_sampleRate);
            break;
        case 2:
            m_coeffs[2][inactive] = computeHighShelf(m_highFreqHz, m_highGainDb, m_sampleRate);
            break;
        default:
            return;
    }

    m_active[band].store(inactive, std::memory_order_release);
}

} // namespace howl::dsp
