// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a Freeverb-style algorithmic reverb, fixed topology and tunings

#include "dsp/Reverb.h"

#include <algorithm>
#include <cmath>

namespace howl::dsp {

namespace {

// Freeverb comb and allpass tunings in samples at 44.1 kHz
constexpr int kCombLengths44k[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
constexpr int kAllpassLengths44k[4] = { 556, 441, 341, 225 };
constexpr int kChannel1Offset = 23;

// Clamps a normalized value to 0..1
float clampNormalized(float value) noexcept {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

// Maps a normalized 0..1 value linearly onto [min, max]
float mapLinear(float value, float min, float max) noexcept {
    return min + clampNormalized(value) * (max - min);
}

} // namespace

// Initializes the normalized parameter cache to the flat defaults (room 0.5, damp 0.5, mix 0.33)
Reverb::Reverb()
    : m_paramValues { 0.5f, 0.5f, 0.33f }
{
}

// Sizes every comb and allpass line from the Freeverb tunings scaled to the rate
void Reverb::prepare(double sampleRate, int) {
    const double scale = sampleRate / 44100.0;

    for (int channel = 0; channel < kMaxChannels; ++channel) {
        const int offset = channel == 1 ? kChannel1Offset : 0;

        for (int i = 0; i < kNumCombs; ++i) {
            const int length = std::max(1, static_cast<int>(std::round((kCombLengths44k[i] + offset) * scale)));
            m_combs[channel][i].buffer.assign(static_cast<std::size_t>(length), 0.0f);
            m_combs[channel][i].pos = 0;
            m_combs[channel][i].filterStore = 0.0f;
        }

        for (int i = 0; i < kNumAllpasses; ++i) {
            const int length = std::max(1, static_cast<int>(std::round((kAllpassLengths44k[i] + offset) * scale)));
            m_allpasses[channel][i].buffer.assign(static_cast<std::size_t>(length), 0.0f);
            m_allpasses[channel][i].pos = 0;
        }
    }
}

// [RT] Runs each channel through its comb bank and series allpasses, mixes dry/wet
void Reverb::process(AudioBlock& audio) noexcept {
    const int channels = audio.numChannels < kMaxChannels ? audio.numChannels : kMaxChannels;

    for (int channel = 0; channel < channels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            const float dry = audio.channels[channel][frame];
            const float scaledInput = dry * 0.015f;

            float combSum = 0.0f;
            for (int i = 0; i < kNumCombs; ++i) {
                combSum += processComb(m_combs[channel][i], scaledInput);
            }

            float wet = combSum;
            for (int i = 0; i < kNumAllpasses; ++i) {
                wet = processAllpass(m_allpasses[channel][i], wet);
            }

            audio.channels[channel][frame] = dry * (1.0f - m_mix) + wet * m_mix;
        }
    }
}

// Zeroes every line and every comb's damping filter state
void Reverb::reset() noexcept {
    for (int channel = 0; channel < kMaxChannels; ++channel) {
        for (int i = 0; i < kNumCombs; ++i) {
            std::fill(m_combs[channel][i].buffer.begin(), m_combs[channel][i].buffer.end(), 0.0f);
            m_combs[channel][i].filterStore = 0.0f;
        }

        for (int i = 0; i < kNumAllpasses; ++i) {
            std::fill(m_allpasses[channel][i].buffer.begin(), m_allpasses[channel][i].buffer.end(), 0.0f);
        }
    }
}

// Returns 0
int Reverb::latencySamples() const noexcept {
    return 0;
}

// Returns 3
int Reverb::numParameters() const {
    return 3;
}

// Returns the name of the parameter at index
const char* Reverb::parameterName(int index) const {
    switch (index) {
        case kRoomSize: return "Room Size";
        case kDamping: return "Damping";
        case kMix: return "Mix";
        default: return "";
    }
}

// [RT] Maps and stores the value, plain float stores, no atomics
void Reverb::setParameter(int index, float value) noexcept {
    if (index < 0 || index >= 3) {
        return;
    }

    m_paramValues[index] = clampNormalized(value);

    switch (index) {
        case kRoomSize:
            m_combFeedback = 0.7f + clampNormalized(value) * 0.28f;
            break;
        case kDamping:
            m_damp = clampNormalized(value) * 0.4f;
            break;
        case kMix:
            m_mix = mapLinear(value, 0.0f, 1.0f);
            break;
        default:
            break;
    }
}

// Returns the last normalized value set for the param at index, its default before any set
float Reverb::getParameter(int index) const noexcept {
    if (index < 0 || index >= 3) {
        return 0.0f;
    }

    return m_paramValues[index];
}

// Returns "Reverb"
const char* Reverb::displayName() const noexcept {
    return "Reverb";
}

// Runs one comb step, advances and wraps its position
float Reverb::processComb(Comb& comb, float input) noexcept {
    const float out = comb.buffer[static_cast<std::size_t>(comb.pos)];
    comb.filterStore = out * (1.0f - m_damp) + comb.filterStore * m_damp;
    comb.buffer[static_cast<std::size_t>(comb.pos)] = input + comb.filterStore * m_combFeedback;
    comb.pos = (comb.pos + 1) % static_cast<int>(comb.buffer.size());
    return out;
}

// Runs one allpass step, advances and wraps its position
float Reverb::processAllpass(Allpass& allpass, float input) noexcept {
    const float bufout = allpass.buffer[static_cast<std::size_t>(allpass.pos)];
    const float out = -input + bufout;
    allpass.buffer[static_cast<std::size_t>(allpass.pos)] = input + bufout * 0.5f;
    allpass.pos = (allpass.pos + 1) % static_cast<int>(allpass.buffer.size());
    return out;
}

} // namespace howl::dsp
