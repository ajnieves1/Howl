// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a sample accurate click mixed into the output, the downbeat of a 4/4 bar accented

#pragma once

#include "core/Types.h"

#include <cmath>
#include <cstdint>

namespace howl::engine {

// Generates a short click on every beat boundary, a brighter louder click on the downbeat
class Metronome {
public:
    // Rearms so the next beat boundary fires a click, call when playback or a count in starts
    void reset() noexcept {
        m_lastBeatIndex = INT64_MIN;
        m_envelope = 0.0f;
    }

    // [RT] Mixes a click for every beat crossed in [blockStartPos, blockStartPos + numFrames)
    void process(AudioBlock& audio, SampleCount blockStartPos, int numFrames, double tempo,
                 double sampleRate) noexcept {
        if (tempo <= 0.0 || sampleRate <= 0.0 || audio.numChannels <= 0) {
            return;
        }

        const double samplesPerBeat = (60.0 / tempo) * sampleRate;
        const double decayPerSample = std::exp(-1.0 / (0.02 * sampleRate)); // about a 20 ms tail
        constexpr double twoPi = 2.0 * 3.14159265358979323846;

        for (int frame = 0; frame < numFrames; ++frame) {
            const SampleCount absPos = blockStartPos + static_cast<SampleCount>(frame);
            const int64_t beatIndex = static_cast<int64_t>(static_cast<double>(absPos) / samplesPerBeat);

            bool newBeat = false;
            if (m_lastBeatIndex == INT64_MIN) {
                // First sample after a reset: fire only when landing on a beat boundary, so
                // starting playback mid beat does not click until the next whole beat
                const double fraction = static_cast<double>(absPos) / samplesPerBeat
                    - static_cast<double>(beatIndex);
                m_lastBeatIndex = beatIndex;
                newBeat = fraction < 1.0 / samplesPerBeat;
            } else if (beatIndex != m_lastBeatIndex) {
                m_lastBeatIndex = beatIndex;
                newBeat = true;
            }

            if (newBeat) {
                m_envelope = 1.0f;
                m_phase = 0.0;
                m_frequency = (beatIndex % 4 == 0) ? 1600.0 : 1000.0; // accent the downbeat
            }

            if (m_envelope > 0.0005f) {
                const float value = m_envelope * static_cast<float>(std::sin(m_phase)) * 0.35f;
                for (int channel = 0; channel < audio.numChannels; ++channel) {
                    audio.channels[channel][frame] += value;
                }
                m_phase += twoPi * m_frequency / sampleRate;
                m_envelope *= static_cast<float>(decayPerSample);
            }
        }
    }

private:
    int64_t m_lastBeatIndex = INT64_MIN;
    double m_phase = 0.0;
    double m_frequency = 1000.0;
    float m_envelope = 0.0f;
};

} // namespace howl::engine
