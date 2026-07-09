// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a fixed 16-voice one shot sample player, pitch follows the key relative to root key 60

#include "dsp/SamplerInstrument.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace howl::dsp {

// Resets every voice, sampleRate and maxBlockSize are unused, pitch never depends on either
void SamplerInstrument::prepare(double /*sampleRate*/, int /*maxBlockSize*/) {
    for (auto& voice : m_voices) {
        voice = Voice {};
    }
}

// Installs the sample and remembers where it came from, device must be paused
void SamplerInstrument::setSample(std::vector<std::vector<float>> channels, std::string sourcePath) {
    m_channels = std::move(channels);
    m_sourcePath = std::move(sourcePath);
    for (auto& voice : m_voices) {
        voice = Voice {};
    }
}

// Returns the path the sample was loaded from, for serialization
const std::string& SamplerInstrument::sourcePath() const {
    return m_sourcePath;
}

// Picks an idle voice first, else the active voice that has played the longest
int SamplerInstrument::findVoiceToSteal() noexcept {
    for (std::size_t i = 0; i < m_voices.size(); ++i) {
        if (!m_voices[i].active) {
            return static_cast<int>(i);
        }
    }

    std::size_t oldestIndex = 0;
    double oldestPlayPos = m_voices[0].playPos;
    for (std::size_t i = 1; i < m_voices.size(); ++i) {
        if (m_voices[i].playPos > oldestPlayPos) {
            oldestPlayPos = m_voices[i].playPos;
            oldestIndex = i;
        }
    }

    return static_cast<int>(oldestIndex);
}

// [RT] Claims a free voice, else steals the longest playing one, starts it at the key's rate
void SamplerInstrument::noteOn(int key, float velocity) noexcept {
    if (m_channels.empty()) {
        return;
    }

    Voice& voice = m_voices[static_cast<std::size_t>(findVoiceToSteal())];
    voice.active = true;
    voice.playPos = 0.0;
    voice.rate = std::pow(2.0, static_cast<double>(key - kRootKey) / 12.0);
    voice.gain = velocity * m_level;
}

// [RT] Documented no-op, one shots play to their own end regardless of note off
void SamplerInstrument::noteOff(int /*key*/) noexcept {
}

// [RT] Overwrites the block, then sums every active voice into it with linear interpolation
void SamplerInstrument::render(AudioBlock& audio) noexcept {
    for (int channel = 0; channel < audio.numChannels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            audio.channels[channel][frame] = 0.0f;
        }
    }

    if (m_channels.empty()) {
        return;
    }

    const std::size_t bufferLength = m_channels[0].size();

    for (auto& voice : m_voices) {
        if (!voice.active) {
            continue;
        }

        for (int frame = 0; frame < audio.numFrames; ++frame) {
            const auto index0 = static_cast<std::size_t>(voice.playPos);
            if (index0 + 1 >= bufferLength) {
                voice.active = false;
                break;
            }

            const double frac = voice.playPos - static_cast<double>(index0);

            for (int channel = 0; channel < audio.numChannels; ++channel) {
                const std::size_t sourceChannel = m_channels.size() == 1
                    ? 0
                    : std::min(static_cast<std::size_t>(channel), m_channels.size() - 1);
                const float a = m_channels[sourceChannel][index0];
                const float b = m_channels[sourceChannel][index0 + 1];
                const float sample = static_cast<float>(a + (b - a) * frac);
                audio.channels[channel][frame] += sample * voice.gain;
            }

            voice.playPos += voice.rate;
        }
    }
}

// Returns 1, this instrument exposes only its output level
int SamplerInstrument::numParameters() const {
    return 1;
}

// Returns the name of the parameter at index
const char* SamplerInstrument::parameterName(int /*index*/) const {
    return "Level";
}

// [RT] Sets the output level, value is normalized 0..1 and used directly as a linear gain
void SamplerInstrument::setParameter(int index, float value) noexcept {
    if (index == kLevelParam) {
        m_level = value;
    }
}

// Returns the last normalized value set for the param at index, its default before any set
float SamplerInstrument::getParameter(int index) const noexcept {
    return index == kLevelParam ? m_level : 0.0f;
}

} // namespace howl::dsp
