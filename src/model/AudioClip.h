// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: already-decoded audio samples for one clip, per channel

#pragma once

#include <cstdint>
#include <vector>

namespace hearth::model {

class AudioClip {
public:
    // channels: one vector per channel, all the same length
    AudioClip(std::vector<std::vector<float>> channels, double sourceSampleRate);

    // Returns the number of channels
    int numChannels() const;

    // Returns the length in samples, 0 if there are no channels
    int64_t lengthSamples() const;

    // Returns the sample rate the audio was recorded or loaded at
    double sourceSampleRate() const;

    // Read-only view of one channel, size lengthSamples()
    const float* channelData(int index) const;

private:
    std::vector<std::vector<float>> m_channels;
    double m_sourceSampleRate;
};

} // namespace hearth::model
