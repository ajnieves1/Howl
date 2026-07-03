// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: WAV file writing and reading on top of JUCE's audio formats

#include "io/AudioFile.h"

namespace howl::io {

// Closes the file if still open
AudioFileWriter::~AudioFileWriter() {
    close();
}

// Opens path for writing at the given sample rate and channel count, returns false on failure
bool AudioFileWriter::open(const std::string& path, double sampleRate, int numChannels) {
    auto stream = std::make_unique<juce::FileOutputStream>(juce::File(path));
    if (stream->failedToOpen()) {
        return false;
    }

    juce::WavAudioFormat format;
    m_writer.reset(format.createWriterFor(stream.get(), sampleRate,
                                           static_cast<unsigned int>(numChannels),
                                           16, {}, 0));
    if (m_writer == nullptr) {
        return false;
    }

    // createWriterFor() took ownership of the stream on success
    stream.release();
    return true;
}

// Appends one block of audio to the file
void AudioFileWriter::write(const AudioBlock& block) {
    if (m_writer != nullptr) {
        m_writer->writeFromFloatArrays(block.channels, block.numChannels, block.numFrames);
    }
}

// Flushes and closes the file
void AudioFileWriter::close() {
    m_writer.reset();
}

// Loads path fully into memory, returns false on failure
bool AudioFileReader::open(const std::string& path) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(juce::File(path)));
    if (reader == nullptr) {
        return false;
    }

    const int numChannels = static_cast<int>(reader->numChannels);
    const int numFrames = static_cast<int>(reader->lengthInSamples);

    m_buffer.setSize(numChannels, numFrames);
    reader->read(&m_buffer, 0, numFrames, 0, true, true);
    m_sampleRate = reader->sampleRate;
    return true;
}

// [RT] Fills block from readPosition, zero-fills any frames past end of file
void AudioFileReader::read(AudioBlock& block, SampleCount readPosition) const noexcept {
    const SampleCount totalFrames = m_buffer.getNumSamples();

    for (int channel = 0; channel < block.numChannels; ++channel) {
        const int sourceChannel = juce::jmin(channel, m_buffer.getNumChannels() - 1);
        const float* source = m_buffer.getReadPointer(sourceChannel);

        for (int frame = 0; frame < block.numFrames; ++frame) {
            const SampleCount sourceIndex = readPosition + frame;
            block.channels[channel][frame] = (sourceIndex >= 0 && sourceIndex < totalFrames)
                ? source[sourceIndex]
                : 0.0f;
        }
    }
}

// Total length of the loaded file in samples
SampleCount AudioFileReader::lengthInSamples() const noexcept {
    return m_buffer.getNumSamples();
}

// Number of channels in the loaded file
int AudioFileReader::numChannels() const noexcept {
    return m_buffer.getNumChannels();
}

// Sample rate the file was recorded at
double AudioFileReader::sampleRate() const noexcept {
    return m_sampleRate;
}

} // namespace howl::io
