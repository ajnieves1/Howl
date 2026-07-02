// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a Freeverb-style algorithmic reverb, fixed topology and tunings

#pragma once

#include "engine/Effect.h"

#include <vector>

namespace howl::dsp {

class Reverb : public engine::Effect {
public:
    // Initializes the normalized parameter cache to the flat defaults (room 0.5, damp 0.5, mix 0.33)
    Reverb();

    // Param indices, every setParameter value is normalized 0..1
    static constexpr int kRoomSize = 0;
    static constexpr int kDamping = 1;
    static constexpr int kMix = 2;

    // Sizes every comb and allpass line from the Freeverb tunings scaled to the rate
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Runs each channel through its comb bank and series allpasses, mixes dry/wet
    void process(AudioBlock& audio) noexcept override;

    // Zeroes every line and every comb's damping filter state
    void reset() noexcept override;

    // Returns 0
    int latencySamples() const noexcept override;

    // Returns 3
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Maps and stores the value, plain float stores, no atomics
    void setParameter(int index, float value) noexcept override;

    // Returns the last normalized value set for the param at index, its default before any set
    float getParameter(int index) const noexcept override;

    // Returns "Reverb"
    const char* displayName() const noexcept override;

private:
    // Freeverb is stereo by construction, channels past 2 pass through untouched
    static constexpr int kMaxChannels = 2;
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 4;

    // One comb filter line with its one-pole damping state
    struct Comb {
        std::vector<float> buffer;
        int pos = 0;
        float filterStore = 0.0f;
    };
    // One allpass line
    struct Allpass {
        std::vector<float> buffer;
        int pos = 0;
    };

    // Runs one comb step, advances and wraps its position
    float processComb(Comb& comb, float input) noexcept;

    // Runs one allpass step, advances and wraps its position
    float processAllpass(Allpass& allpass, float input) noexcept;

    Comb m_combs[kMaxChannels][kNumCombs];
    Allpass m_allpasses[kMaxChannels][kNumAllpasses];
    float m_combFeedback = 0.84f;
    float m_damp = 0.2f;
    float m_mix = 0.33f;
    // Raw clamped normalized values, one slot per param index
    float m_paramValues[3];
};

} // namespace howl::dsp
