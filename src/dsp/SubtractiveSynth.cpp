// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a fixed 16-voice subtractive synth, oscillator into a lowpass filter into an ADSR

#include "dsp/SubtractiveSynth.h"

#include <cmath>

namespace howl::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kAttackSeconds = 0.005;
constexpr double kDecaySeconds = 0.05;
constexpr float kSustainLevel = 0.7f;
constexpr double kReleaseSeconds = 0.2;
constexpr double kFilterCutoffHz = 4000.0;
} // namespace

// Converts a MIDI key number to frequency in Hz
double SubtractiveSynth::keyToFrequency(int key) noexcept {
    return 440.0 * std::pow(2.0, (key - 69) / 12.0);
}

// Resets every voice and recomputes envelope and filter rates for sampleRate
void SubtractiveSynth::prepare(double sampleRate, int /*maxBlockSize*/) {
    m_sampleRate = sampleRate;
    m_sampleCounter = 0;

    m_attackRate = static_cast<float>(1.0 / (kAttackSeconds * sampleRate));
    m_decayRate = static_cast<float>((1.0 - kSustainLevel) / (kDecaySeconds * sampleRate));
    m_sustainLevel = kSustainLevel;
    m_releaseRate = static_cast<float>(1.0 / (kReleaseSeconds * sampleRate));

    m_filterCoefficient = static_cast<float>(1.0 - std::exp(-kTwoPi * kFilterCutoffHz / sampleRate));

    for (auto& voice : m_voices) {
        voice = Voice {};
    }
}

// Picks an idle voice first, then a releasing voice, then the oldest voice
int SubtractiveSynth::findVoiceToSteal() noexcept {
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        if (!m_voices[i].active) {
            return static_cast<int>(i);
        }
    }

    std::size_t releasingIndex = m_voices.size();
    std::size_t oldestIndex = 0;
    uint64_t oldestTime = m_voices[0].triggeredAtSample;

    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        if (m_voices[i].stage == EnvelopeStage::Release && releasingIndex == m_voices.size()) {
            releasingIndex = i;
        }
        if (m_voices[i].triggeredAtSample < oldestTime) {
            oldestTime = m_voices[i].triggeredAtSample;
            oldestIndex = i;
        }
    }

    return static_cast<int>(releasingIndex != m_voices.size() ? releasingIndex : oldestIndex);
}

// [RT] Assigns or steals a voice, starts its attack stage
void SubtractiveSynth::noteOn(int key, float velocity) noexcept {
    const int index = findVoiceToSteal();
    Voice& voice = m_voices[static_cast<std::size_t>(index)];

    voice.active = true;
    voice.key = key;
    voice.velocity = velocity;
    voice.phase = 0.0;
    voice.phaseIncrement = keyToFrequency(key) / m_sampleRate;
    voice.filterState = 0.0f;
    voice.stage = EnvelopeStage::Attack;
    voice.envelopeLevel = 0.0f;
    voice.triggeredAtSample = m_sampleCounter;
}

// [RT] Moves every active voice matching key into its release stage
void SubtractiveSynth::noteOff(int key) noexcept {
    for (auto& voice : m_voices) {
        if (voice.active && voice.key == key && voice.stage != EnvelopeStage::Release) {
            voice.stage = EnvelopeStage::Release;
        }
    }
}

// [RT] Advances one voice's envelope, oscillator, and filter by one sample
float SubtractiveSynth::renderVoiceSample(Voice& voice) noexcept {
    switch (voice.stage) {
        case EnvelopeStage::Attack:
            voice.envelopeLevel += m_attackRate;
            if (voice.envelopeLevel >= 1.0f) {
                voice.envelopeLevel = 1.0f;
                voice.stage = EnvelopeStage::Decay;
            }
            break;
        case EnvelopeStage::Decay:
            voice.envelopeLevel -= m_decayRate;
            if (voice.envelopeLevel <= m_sustainLevel) {
                voice.envelopeLevel = m_sustainLevel;
                voice.stage = EnvelopeStage::Sustain;
            }
            break;
        case EnvelopeStage::Sustain:
            break;
        case EnvelopeStage::Release:
            voice.envelopeLevel -= m_releaseRate;
            if (voice.envelopeLevel <= 0.0f) {
                voice.envelopeLevel = 0.0f;
                voice.stage = EnvelopeStage::Idle;
                voice.active = false;
            }
            break;
        case EnvelopeStage::Idle:
            return 0.0f;
    }

    const float oscillatorSample = static_cast<float>(2.0 * voice.phase - 1.0);
    voice.phase += voice.phaseIncrement;
    if (voice.phase >= 1.0) {
        voice.phase -= 1.0;
    }

    voice.filterState += m_filterCoefficient * (oscillatorSample - voice.filterState);

    return voice.filterState * voice.envelopeLevel * voice.velocity;
}

// [RT] Clears the block, then sums every active voice into it
void SubtractiveSynth::render(AudioBlock& audio) noexcept {
    for (int channel = 0; channel < audio.numChannels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            audio.channels[channel][frame] = 0.0f;
        }
    }

    for (auto& voice : m_voices) {
        if (!voice.active) {
            continue;
        }

        for (int frame = 0; frame < audio.numFrames; ++frame) {
            const float sample = renderVoiceSample(voice);
            for (int channel = 0; channel < audio.numChannels; ++channel) {
                audio.channels[channel][frame] += sample;
            }
        }
    }

    m_sampleCounter += static_cast<uint64_t>(audio.numFrames);
}

} // namespace howl::dsp
