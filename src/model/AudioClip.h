// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: already-decoded audio samples for one clip, per channel

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace howl::model {

class AudioClip {
public:
    // channels: one vector per channel, all the same length
    AudioClip(std::vector<std::vector<float>> channels, double sourceSampleRate);

    // Empty placeholder clip (no samples), used when loading a project before the
    // source file at sourcePath() is re-read and the real samples are filled in
    AudioClip();

    // Returns the number of channels
    int numChannels() const;

    // Returns the length in samples, 0 if there are no channels
    int64_t lengthSamples() const;

    // Returns the sample rate the audio was recorded or loaded at
    double sourceSampleRate() const;

    // Read-only view of one channel, size lengthSamples()
    const float* channelData(int index) const;

    // Absolute path of the file this clip was imported from, empty if none, used by persistence
    void setSourcePath(const std::string& path);

    // Returns the source file path, empty if this clip was not imported from a file
    const std::string& sourcePath() const;

private:
    std::vector<std::vector<float>> m_channels;
    double m_sourceSampleRate;
    std::string m_sourcePath;
};

} // namespace howl::model
