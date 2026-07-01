// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: records input to disk via a ring buffer, plays a file back

#pragma once

#include "core/LockFreeQueue.h"
#include "core/Types.h"
#include "io/AudioFile.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace howl::engine {

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
    // Sanity bound on requested channel count, not a storage size
    static constexpr int kMaxChannels = 64;
    static constexpr std::size_t kRingBufferCapacity = 16384;

    // Runs on m_writerThread until stopRecording() clears m_recording
    void writerThreadLoop();

    // Allocated lazily in startRecording(), one per channel, never touched
    // by a track that only ever plays back
    std::vector<std::unique_ptr<LockFreeQueue<float, kRingBufferCapacity>>> m_ringBuffers;
    int m_numChannels = 0;

    io::AudioFileWriter m_writer;
    std::thread m_writerThread;
    std::atomic<bool> m_recording { false };

    io::AudioFileReader m_reader;
};

} // namespace howl::engine
