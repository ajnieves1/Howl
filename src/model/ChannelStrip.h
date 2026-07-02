// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per-track gain, pan, mute, solo, and effects chain

#pragma once

#include "core/Types.h"
#include "engine/EffectChain.h"

namespace howl::model {

class ChannelStrip {
public:
    // Sets the gain in decibels, 0 dB is unity
    void setGainDb(float db);

    // Returns the current gain in decibels
    float gainDb() const;

    // Sets the pan, -1 is hard left, 0 is centre, +1 is hard right, balance law
    void setPan(float pan);

    // Returns the current pan
    float pan() const;

    // Sets whether the strip is muted
    void setMuted(bool muted);

    // Returns whether the strip is muted
    bool muted() const;

    // Sets whether the strip is soloed
    void setSoloed(bool soloed);

    // Returns whether the strip is soloed
    bool soloed() const;

    // Returns the strip's effect chain
    engine::EffectChain& effects();

    // [RT] Runs the FX chain in place, no gain, pan, or mute applied
    void processEffects(AudioBlock& audio) noexcept;

    // [RT] Applies gain and pan in place, no FX or mute applied
    void applyGain(AudioBlock& audio) noexcept;

    // [RT] Runs the FX chain then applies gain and pan in place
    void process(AudioBlock& audio) noexcept;

private:
    float m_gainDb = 0.0f;
    float m_pan = 0.0f;
    bool m_muted = false;
    bool m_soloed = false;
    engine::EffectChain m_effects;
};

} // namespace howl::model
