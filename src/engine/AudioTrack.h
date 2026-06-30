// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: records input to disk via a ring buffer, plays a file back

#pragma once

#include "core/LockFreeQueue.h"
#include "core/Types.h"
#include "io/AudioFile.h"

#include <array>
#include <atomic>
#include <string>
#include <thread>

namespace hearth::engine {

class AudioTrack {
public:
    // Stops any in-progress recording
    ~AudioTrack();

    // Opens path for writing and starts the writer thread, returns false on failure
    bool startRecording(const std::string& path, double sampleRate, int numChannels);

    // Stops the writer thread, flushing any remaining samples to disk
    void stopRecording();

    // [RT] Pushes one block of input into the ring buffer, drops samples if the writer falls behind
    void captureBlock(const AudioBlock& input) noexcept;

    // Loads a previously recorded file for playback, never call from the audio thread
    bool loadForPlayback(const std::string& path);

    // [RT] Fills output with previously recorded audio starting at readPosition
    void renderBlock(AudioBlock& output, SampleCount readPosition) const noexcept;

private:
    static constexpr int kMaxChannels = 8;
    static constexpr std::size_t kRingBufferCapacity = 16384;

    // Runs on m_writerThread until stopRecording() clears m_recording
    void writerThreadLoop();

    std::array<LockFreeQueue<float, kRingBufferCapacity>, kMaxChannels> m_ringBuffers;
    int m_numChannels = 0;

    io::AudioFileWriter m_writer;
    std::thread m_writerThread;
    std::atomic<bool> m_recording { false };

    io::AudioFileReader m_reader;
};

} // namespace hearth::engine
