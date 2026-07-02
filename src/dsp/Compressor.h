// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a feed-forward compressor with linked-stereo detection

#pragma once

#include "dsp/EnvelopeFollower.h"
#include "engine/Effect.h"

namespace howl::dsp {

class Compressor : public engine::Effect {
public:
    // Initializes the normalized parameter cache from the real-unit defaults
    Compressor();

    // Param indices, every setParameter value is normalized 0..1
    static constexpr int kThreshold = 0;
    static constexpr int kRatio = 1;
    static constexpr int kAttack = 2;
    static constexpr int kRelease = 3;
    static constexpr int kMakeup = 4;

    // Stores the sample rate and recomputes the envelope coefficients
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Applies feed-forward gain reduction per frame, one linked gain for all channels
    void process(AudioBlock& audio) noexcept override;

    // Clears the envelope
    void reset() noexcept override;

    // Returns 0, feed-forward with no lookahead
    int latencySamples() const noexcept override;

    // Returns 5
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Maps and stores the value, re-prepares the envelope if the rate is known
    void setParameter(int index, float value) noexcept override;

    // Returns the last normalized value set for the param at index, its default before any set
    float getParameter(int index) const noexcept override;

    // Returns "Compressor"
    const char* displayName() const noexcept override;

private:
    EnvelopeFollower m_envelope;
    float m_thresholdDb = -20.0f;
    float m_ratio = 4.0f;
    float m_attackMs = 10.0f;
    float m_releaseMs = 100.0f;
    float m_makeupDb = 0.0f;
    double m_sampleRate = 0.0;
    // Normalized cache, one slot per param index, declared last so the real-unit
    // defaults above are already set when this is constructed
    float m_paramValues[5];
};

} // namespace howl::dsp
