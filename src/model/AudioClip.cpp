// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: already-decoded audio samples for one clip, per channel

#include "model/AudioClip.h"

namespace hearth::model {

// channels: one vector per channel, all the same length
AudioClip::AudioClip(std::vector<std::vector<float>> channels, double sourceSampleRate)
    : m_channels(std::move(channels))
    , m_sourceSampleRate(sourceSampleRate)
{
}

// Returns the number of channels
int AudioClip::numChannels() const {
    return static_cast<int>(m_channels.size());
}

// Returns the length in samples, 0 if there are no channels
int64_t AudioClip::lengthSamples() const {
    return m_channels.empty() ? 0 : static_cast<int64_t>(m_channels[0].size());
}

// Returns the sample rate the audio was recorded or loaded at
double AudioClip::sourceSampleRate() const {
    return m_sourceSampleRate;
}

// Read-only view of one channel, size lengthSamples()
const float* AudioClip::channelData(int index) const {
    return m_channels[static_cast<std::size_t>(index)].data();
}

} // namespace hearth::model
