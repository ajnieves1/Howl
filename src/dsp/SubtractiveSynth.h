// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a fixed 16-voice subtractive synth, oscillator into a lowpass filter into an ADSR

#pragma once

#include "engine/Instrument.h"

#include <array>
#include <cstdint>

namespace howl::dsp {

class SubtractiveSynth : public engine::Instrument {
public:
    // Resets every voice and recomputes envelope and filter rates for sampleRate
    void prepare(double sampleRate, int maxBlockSize) override;

    // [RT] Assigns or steals a voice, starts its attack stage
    void noteOn(int key, float velocity) noexcept override;

    // [RT] Moves every active voice matching key into its release stage
    void noteOff(int key) noexcept override;

    // [RT] Clears the block, then sums every active voice into it
    void render(AudioBlock& audio) noexcept override;

    // Returns 1, this synth exposes only the filter cutoff so far
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Sets the filter cutoff, value is normalized 0..1 mapped log-scale to Hz
    void setParameter(int index, float value) noexcept override;

private:
    static constexpr int kNumVoices = 16;
    static constexpr int kFilterCutoffParam = 0;
    static constexpr double kMinFilterCutoffHz = 200.0;
    static constexpr double kMaxFilterCutoffHz = 8000.0;

    enum class EnvelopeStage {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    struct Voice {
        bool active = false;
        int key = -1;
        float velocity = 0.0f;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        float filterState = 0.0f;
        EnvelopeStage stage = EnvelopeStage::Idle;
        float envelopeLevel = 0.0f;
        uint64_t triggeredAtSample = 0;
    };

    // Picks an idle voice first, then a releasing voice, then the oldest voice
    int findVoiceToSteal() noexcept;

    // Converts a MIDI key number to frequency in Hz
    static double keyToFrequency(int key) noexcept;

    // [RT] Advances one voice's envelope, oscillator, and filter by one sample
    float renderVoiceSample(Voice& voice) noexcept;

    // [RT] Recomputes the one-pole filter coefficient from m_filterCutoffHz and m_sampleRate
    void recomputeFilterCoefficient() noexcept;

    std::array<Voice, kNumVoices> m_voices;
    double m_sampleRate = 44100.0;
    uint64_t m_sampleCounter = 0;

    double m_filterCutoffHz = 4000.0;
    float m_filterCoefficient = 0.0f;

    float m_attackRate = 0.0f;
    float m_decayRate = 0.0f;
    float m_sustainLevel = 0.7f;
    float m_releaseRate = 0.0f;
};

} // namespace howl::dsp
