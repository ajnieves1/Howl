// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a fixed three-band parametric EQ (low shelf, peak, high shelf)

#pragma once

#include "engine/Effect.h"

#include <atomic>

namespace howl::dsp {

// One normalized biquad, a0 already divided out
struct BiquadCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

class Equalizer : public engine::Effect {
public:
    // Initializes the normalized parameter cache from the flat-EQ real-unit defaults
    Equalizer();

    // Param indices, every setParameter value is normalized 0..1
    static constexpr int kLowFreq = 0;
    static constexpr int kLowGain = 1;
    static constexpr int kPeakFreq = 2;
    static constexpr int kPeakGain = 3;
    static constexpr int kPeakQ = 4;
    static constexpr int kHighFreq = 5;
    static constexpr int kHighGain = 6;

    // Stores the sample rate and recomputes every band's coefficients
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Runs the three bands in series, per channel, direct form II transposed
    void process(AudioBlock& audio) noexcept override;

    // Zeroes the filter state, coefficients are untouched
    void reset() noexcept override;

    // Returns 0, IIR filters add no block latency
    int latencySamples() const noexcept override;

    // Returns 7
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Maps and stores the value, then republishes the affected band's coefficients
    void setParameter(int index, float value) noexcept override;

    // Returns the last normalized value set for the param at index, its default before any set
    float getParameter(int index) const noexcept override;

    // Returns "EQ"
    const char* displayName() const noexcept override;

private:
    static constexpr int kNumBands = 3;
    static constexpr int kMaxChannels = 8;

    // Recomputes one band's coefficients into its inactive slot and publishes it
    void updateBand(int band) noexcept;

    // Two coefficient slots per band, the atomic index picks the one process() reads
    BiquadCoeffs m_coeffs[kNumBands][2];
    std::atomic<int> m_active[kNumBands] { 0, 0, 0 };
    // Direct form II transposed state, [band][channel][z1, z2]
    float m_state[kNumBands][kMaxChannels][2] {};
    // Current mapped real-unit values, the defaults are the flat EQ
    float m_lowFreqHz = 200.0f;
    float m_lowGainDb = 0.0f;
    float m_peakFreqHz = 1000.0f;
    float m_peakGainDb = 0.0f;
    float m_peakQ = 0.70710678f;
    float m_highFreqHz = 5000.0f;
    float m_highGainDb = 0.0f;
    double m_sampleRate = 0.0;
    // Normalized cache, one slot per param index, declared last so the real-unit
    // defaults above are already set when this is constructed
    float m_paramValues[7];
};

} // namespace howl::dsp
