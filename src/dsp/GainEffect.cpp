// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a single-parameter gain effect, multiplies every sample

#include "dsp/GainEffect.h"

#include <cmath>

namespace howl::dsp {

// Prepares the effect, gain has no rate or block size dependence
void GainEffect::prepare(double, int) {
}

// [RT] Multiplies every sample in the block by the current linear gain
void GainEffect::process(AudioBlock& audio) noexcept {
    for (int channel = 0; channel < audio.numChannels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            audio.channels[channel][frame] *= m_gainLinear;
        }
    }
}

// No internal state to clear
void GainEffect::reset() noexcept {
}

// Returns 0, this effect has no processing latency
int GainEffect::latencySamples() const noexcept {
    return 0;
}

// Returns 1, this effect exposes only its gain
int GainEffect::numParameters() const {
    return 1;
}

// Returns the name of the parameter at index
const char* GainEffect::parameterName(int index) const {
    if (index != kGainParam) {
        return "";
    }

    return "Gain";
}

// [RT] Sets the gain, value is normalized 0..1 mapped to a dB range
void GainEffect::setParameter(int index, float value) noexcept {
    if (index != kGainParam) {
        return;
    }

    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    const float gainDb = kMinGainDb + clamped * (kMaxGainDb - kMinGainDb);
    m_gainLinear = std::pow(10.0f, gainDb / 20.0f);
}

// Returns "Gain"
const char* GainEffect::displayName() const noexcept {
    return "Gain";
}

} // namespace howl::dsp
