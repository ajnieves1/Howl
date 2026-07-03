// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: already-decoded audio samples for one clip, per channel

#include "model/AudioClip.h"

namespace howl::model {

// channels: one vector per channel, all the same length
AudioClip::AudioClip(std::vector<std::vector<float>> channels, double sourceSampleRate)
    : m_channels(std::move(channels))
    , m_sourceSampleRate(sourceSampleRate)
{
}

// Empty placeholder clip (no samples), used when loading a project before the
// source file at sourcePath() is re-read and the real samples are filled in
AudioClip::AudioClip()
    : AudioClip(std::vector<std::vector<float>> {}, 0.0)
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

// Absolute path of the file this clip was imported from, empty if none, used by persistence
void AudioClip::setSourcePath(const std::string& path) {
    m_sourcePath = path;
}

// Returns the source file path, empty if this clip was not imported from a file
const std::string& AudioClip::sourcePath() const {
    return m_sourcePath;
}

// Tempo the source material was recorded at, 0 when unknown
void AudioClip::setOriginalBpm(double bpm) {
    m_originalBpm = bpm;
}

// Returns the tempo the source material was recorded at, 0 when unknown
double AudioClip::originalBpm() const {
    return m_originalBpm;
}

// Whether playback should use warped buffers when they are present
void AudioClip::setWarpEnabled(bool enabled) {
    m_warpEnabled = enabled;
}

// Returns whether playback should use warped buffers when they are present
bool AudioClip::warpEnabled() const {
    return m_warpEnabled;
}

// Installs stretched channels rendered for a project tempo, off the audio thread, device paused
void AudioClip::setWarpedChannels(std::vector<std::vector<float>> channels, double projectTempo) {
    m_warpedChannels = std::move(channels);
    m_warpedTempo = projectTempo;
}

// Drops the warped buffers, playback falls back to the source samples
void AudioClip::clearWarpedChannels() {
    m_warpedChannels.clear();
    m_warpedTempo = 0.0;
}

// Returns the tempo the current warped buffers were rendered for, 0 when none
double AudioClip::warpedTempo() const {
    return m_warpedTempo;
}

// [RT] Channel data playback uses: warped when enabled and present, source otherwise
const float* AudioClip::activeChannelData(int index) const noexcept {
    if (m_warpEnabled && !m_warpedChannels.empty()) {
        return m_warpedChannels[static_cast<std::size_t>(index)].data();
    }
    return m_channels[static_cast<std::size_t>(index)].data();
}

// [RT] Length matching activeChannelData
int64_t AudioClip::activeLengthSamples() const noexcept {
    if (m_warpEnabled && !m_warpedChannels.empty()) {
        return static_cast<int64_t>(m_warpedChannels[0].size());
    }
    return lengthSamples();
}

} // namespace howl::model
