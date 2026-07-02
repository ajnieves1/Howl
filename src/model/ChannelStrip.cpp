// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per-track gain, pan, mute, solo, and effects chain

#include "model/ChannelStrip.h"

#include <cmath>

namespace howl::model {

// Sets the gain in decibels, 0 dB is unity
void ChannelStrip::setGainDb(float db) {
    m_gainDb = db;
}

// Returns the current gain in decibels
float ChannelStrip::gainDb() const {
    return m_gainDb;
}

// Sets the pan, -1 is hard left, 0 is centre, +1 is hard right, balance law
void ChannelStrip::setPan(float pan) {
    m_pan = pan;
}

// Returns the current pan
float ChannelStrip::pan() const {
    return m_pan;
}

// Sets whether the strip is muted
void ChannelStrip::setMuted(bool muted) {
    m_muted = muted;
}

// Returns whether the strip is muted
bool ChannelStrip::muted() const {
    return m_muted;
}

// Sets whether the strip is soloed
void ChannelStrip::setSoloed(bool soloed) {
    m_soloed = soloed;
}

// Returns whether the strip is soloed
bool ChannelStrip::soloed() const {
    return m_soloed;
}

// Returns the strip's effect chain
engine::EffectChain& ChannelStrip::effects() {
    return m_effects;
}

// Returns the strip's effect chain, const overload for latency walks
const engine::EffectChain& ChannelStrip::effects() const {
    return m_effects;
}

// [RT] Runs the FX chain in place, no gain, pan, or mute applied
void ChannelStrip::processEffects(AudioBlock& audio) noexcept {
    m_effects.process(audio);
}

// [RT] Applies gain and pan in place, no FX or mute applied
void ChannelStrip::applyGain(AudioBlock& audio) noexcept {
    const float gainLinear = std::pow(10.0f, m_gainDb / 20.0f);
    const float leftGain = m_pan <= 0.0f ? 1.0f : (1.0f - m_pan);
    const float rightGain = m_pan >= 0.0f ? 1.0f : (1.0f + m_pan);

    for (int channel = 0; channel < audio.numChannels; ++channel) {
        const float panGain = channel == 0 ? leftGain : (channel == 1 ? rightGain : 1.0f);
        const float channelGain = gainLinear * panGain;

        for (int frame = 0; frame < audio.numFrames; ++frame) {
            audio.channels[channel][frame] *= channelGain;
        }
    }
}

// [RT] Runs the FX chain then applies gain and pan in place
void ChannelStrip::process(AudioBlock& audio) noexcept {
    if (m_muted) {
        for (int channel = 0; channel < audio.numChannels; ++channel) {
            for (int frame = 0; frame < audio.numFrames; ++frame) {
                audio.channels[channel][frame] = 0.0f;
            }
        }

        return;
    }

    processEffects(audio);
    applyGain(audio);
}

} // namespace howl::model
