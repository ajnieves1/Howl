// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: WAV file writing and reading on top of JUCE's audio formats

#pragma once

#include "core/Types.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <string>

namespace howl::io {

// Appends float blocks to a 16-bit WAV file, never call from the audio thread
class AudioFileWriter {
public:
    // Closes the file if still open
    ~AudioFileWriter();

    // Opens path for writing at the given sample rate and channel count, returns false on failure
    bool open(const std::string& path, double sampleRate, int numChannels);

    // Appends one block of audio to the file
    void write(const AudioBlock& block);

    // Flushes and closes the file
    void close();

private:
    std::unique_ptr<juce::AudioFormatWriter> m_writer;
};

// Loads a whole WAV file into memory once, then serves [RT]-safe reads from it
class AudioFileReader {
public:
    // Loads path fully into memory, returns false on failure
    bool open(const std::string& path);

    // [RT] Fills block from readPosition, zero-fills any frames past end of file
    void read(AudioBlock& block, SampleCount readPosition) const noexcept;

    // Total length of the loaded file in samples
    SampleCount lengthInSamples() const noexcept;

    // Number of channels in the loaded file
    int numChannels() const noexcept;

    // Sample rate the file was recorded at
    double sampleRate() const noexcept;

private:
    juce::AudioBuffer<float> m_buffer;
    double m_sampleRate = 0.0;
};

} // namespace howl::io
