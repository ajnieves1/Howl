// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a fixed 16-voice one shot sample player, pitch follows the key relative to root key 60

#pragma once

#include "engine/Instrument.h"

#include <array>
#include <string>
#include <vector>

namespace howl::dsp {

// One shot sample player, pitch follows the key relative to root key 60
class SamplerInstrument : public engine::Instrument {
public:
    // Resets every voice, sampleRate and maxBlockSize are unused, pitch never depends on either
    void prepare(double sampleRate, int maxBlockSize) override;

    // Installs the sample and remembers where it came from, device must be paused
    void setSample(std::vector<std::vector<float>> channels, std::string sourcePath);

    // Returns the path the sample was loaded from, for serialization
    const std::string& sourcePath() const;

    // [RT] Claims a free voice, else steals the longest playing one, starts it at the key's rate
    void noteOn(int key, float velocity) noexcept override;

    // [RT] Documented no-op, one shots play to their own end regardless of note off
    void noteOff(int key) noexcept override;

    // [RT] Overwrites the block, then sums every active voice into it with linear interpolation
    void render(AudioBlock& audio) noexcept override;

    // Returns 1, this instrument exposes only its output level
    int numParameters() const override;

    // Returns the name of the parameter at index
    const char* parameterName(int index) const override;

    // [RT] Sets the output level, value is normalized 0..1 and used directly as a linear gain
    void setParameter(int index, float value) noexcept override;

    // Returns the last normalized value set for the param at index, its default before any set
    float getParameter(int index) const noexcept override;

private:
    static constexpr int kNumVoices = 16;
    static constexpr int kLevelParam = 0;
    static constexpr float kDefaultLevel = 0.8f;
    static constexpr int kRootKey = 60;

    struct Voice {
        double playPos = 0.0;
        double rate = 1.0;
        float gain = 0.0f;
        bool active = false;
    };

    // Picks an idle voice first, else the active voice that has played the longest
    int findVoiceToSteal() noexcept;

    std::array<Voice, kNumVoices> m_voices;
    std::vector<std::vector<float>> m_channels;
    std::string m_sourcePath;
    float m_level = kDefaultLevel;
};

} // namespace howl::dsp
