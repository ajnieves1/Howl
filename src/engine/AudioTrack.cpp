// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: records input to disk via a ring buffer, plays a file back

#include "engine/AudioTrack.h"

#include <chrono>
#include <vector>

namespace hearth::engine {

// Stops any in-progress recording
AudioTrack::~AudioTrack() {
    stopRecording();
}

// Opens path for writing and starts the writer thread, returns false on failure
bool AudioTrack::startRecording(const std::string& path, double sampleRate, int numChannels) {
    if (numChannels <= 0 || numChannels > kMaxChannels) {
        return false;
    }

    if (!m_writer.open(path, sampleRate, numChannels)) {
        return false;
    }

    m_numChannels = numChannels;
    m_ringBuffers.clear();
    for (int channel = 0; channel < numChannels; ++channel) {
        m_ringBuffers.push_back(std::make_unique<LockFreeQueue<float, kRingBufferCapacity>>());
    }

    m_recording.store(true, std::memory_order_release);
    m_writerThread = std::thread([this] { writerThreadLoop(); });
    return true;
}

// Stops the writer thread, flushing any remaining samples to disk
void AudioTrack::stopRecording() {
    m_recording.store(false, std::memory_order_release);
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }
    m_writer.close();
}

// [RT] Pushes one block of input into the ring buffer, drops samples if the writer falls behind
void AudioTrack::captureBlock(const AudioBlock& input) noexcept {
    if (!m_recording.load(std::memory_order_acquire)) {
        return;
    }

    const int channelsToCapture = juce::jmin(m_numChannels, input.numChannels);
    for (int channel = 0; channel < channelsToCapture; ++channel) {
        for (int frame = 0; frame < input.numFrames; ++frame) {
            m_ringBuffers[static_cast<std::size_t>(channel)]->push(input.channels[channel][frame]);
        }
    }
}

// Loads a previously recorded file for playback, never call from the audio thread
bool AudioTrack::loadForPlayback(const std::string& path) {
    return m_reader.open(path);
}

// [RT] Fills output with previously recorded audio starting at readPosition
void AudioTrack::renderBlock(AudioBlock& output, SampleCount readPosition) const noexcept {
    m_reader.read(output, readPosition);
}

// Runs on m_writerThread until stopRecording() clears m_recording
void AudioTrack::writerThreadLoop() {
    std::vector<std::vector<float>> channelBuffers(static_cast<std::size_t>(m_numChannels));
    std::vector<float*> channelPointers(static_cast<std::size_t>(m_numChannels));

    const auto drainOnce = [&] {
        for (int channel = 0; channel < m_numChannels; ++channel) {
            channelBuffers[static_cast<std::size_t>(channel)].clear();
            float sample = 0.0f;
            while (m_ringBuffers[static_cast<std::size_t>(channel)]->pop(sample)) {
                channelBuffers[static_cast<std::size_t>(channel)].push_back(sample);
            }
        }

        const int framesPopped = static_cast<int>(channelBuffers[0].size());
        if (framesPopped > 0) {
            for (int channel = 0; channel < m_numChannels; ++channel) {
                channelPointers[static_cast<std::size_t>(channel)] = channelBuffers[static_cast<std::size_t>(channel)].data();
            }

            AudioBlock block { channelPointers.data(), m_numChannels, framesPopped };
            m_writer.write(block);
        }
    };

    while (m_recording.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        drainOnce();
    }

    // One last drain to flush whatever arrived right before stopRecording()
    drainOnce();
}

} // namespace hearth::engine
