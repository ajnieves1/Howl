// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a feedback delay with an integer-sample ring buffer per channel

#include "dsp/Delay.h"

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
Delay::Delay()
    : m_paramValues {
        inverseExponential(m_timeMs, 1.0f, 2000.0f),
        inverseLinear(m_feedback, 0.0f, 0.95f),
        inverseLinear(m_mix, 0.0f, 1.0f)
      }
{
}

// Sizes one ring buffer per channel and recomputes the delay in samples
void Delay::prepare(double sampleRate, int) {
    m_sampleRate = sampleRate;
    m_lineSize = static_cast<int>(kMaxDelaySeconds * sampleRate) + 1;
    m_lines.assign(static_cast<std::size_t>(kMaxChannels), std::vector<float>(static_cast<std::size_t>(m_lineSize), 0.0f));
    m_writePos = 0;
    updateDelaySamples();
}

// [RT] Reads the wet tap, writes dry plus feedback back in, mixes dry/wet
void Delay::process(AudioBlock& audio) noexcept {
    const int channels = audio.numChannels < kMaxChannels ? audio.numChannels : kMaxChannels;

    for (int frame = 0; frame < audio.numFrames; ++frame) {
        const int readPos = (m_writePos - m_delaySamples + m_lineSize) % m_lineSize;

        for (int channel = 0; channel < channels; ++channel) {
            const float dry = audio.channels[channel][frame];
            const float wet = m_lines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(readPos)];
            m_lines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(m_writePos)] = dry + wet * m_feedback;
            audio.channels[channel][frame] = dry * (1.0f - m_mix) + wet * m_mix;
        }

        m_writePos = (m_writePos + 1) % m_lineSize;
    }
}

// Zeroes every delay line
void Delay::reset() noexcept {
    for (auto& line : m_lines) {
        std::fill(line.begin(), line.end(), 0.0f);
    }
}

// Returns 0, the dry path is never delayed
int Delay::latencySamples() const noexcept {
    return 0;
}

// Returns 3
int Delay::numParameters() const {
    return 3;
}

// Returns the name of the parameter at index
const char* Delay::parameterName(int index) const {
    switch (index) {
        case kTime: return "Time";
        case kFeedback: return "Feedback";
        case kMix: return "Mix";
        default: return "";
    }
}

// [RT] Maps and stores the value, recomputes the delay in samples if the rate is known
void Delay::setParameter(int index, float value) noexcept {
    if (index < 0 || index >= 3) {
        return;
    }

    m_paramValues[index] = clampNormalized(value);

    switch (index) {
        case kTime:
            m_timeMs = mapExponential(value, 1.0f, 2000.0f);
            if (m_sampleRate > 0.0) {
                updateDelaySamples();
            }
            break;
        case kFeedback:
            m_feedback = mapLinear(value, 0.0f, 0.95f);
            break;
        case kMix:
            m_mix = mapLinear(value, 0.0f, 1.0f);
            break;
        default:
            break;
    }
}

// Returns the last normalized value set for the param at index, its default before any set
float Delay::getParameter(int index) const noexcept {
    if (index < 0 || index >= 3) {
        return 0.0f;
    }

    return m_paramValues[index];
}

// Returns "Delay"
const char* Delay::displayName() const noexcept {
    return "Delay";
}

// Recomputes m_delaySamples from m_timeMs and the stored sample rate
void Delay::updateDelaySamples() noexcept {
    const int computed = static_cast<int>(std::round(m_timeMs * m_sampleRate / 1000.0));
    m_delaySamples = std::max(1, std::min(computed, m_lineSize - 1));
}

} // namespace howl::dsp
