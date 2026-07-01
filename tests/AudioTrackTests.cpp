// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AudioTrack record-then-playback round-trip test

#include "engine/AudioTrack.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using howl::AudioBlock;
using howl::engine::AudioTrack;

namespace {

// Root-mean-square of a flat sample buffer
double rms(const std::vector<float>& samples) {
    double sumSquares = 0.0;
    for (float sample : samples) {
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return std::sqrt(sumSquares / static_cast<double>(samples.size()));
}

} // namespace

TEST_CASE("AudioTrack records to disk and plays the recording back, RMS within tolerance", "[audiotrack]") {
    const double sampleRate = 1000.0;
    const int numFrames = 5000; // 5 seconds at the test sample rate
    const int blockSize = 100;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "howl_audiotrack_test.wav";

    std::vector<float> originalSamples(static_cast<std::size_t>(numFrames));
    for (int i = 0; i < numFrames; ++i) {
        originalSamples[static_cast<std::size_t>(i)] = std::sin(static_cast<float>(i) * 0.1f) * 0.5f;
    }

    AudioTrack recordTrack;
    REQUIRE(recordTrack.startRecording(path.string(), sampleRate, 1));

    for (int offset = 0; offset < numFrames; offset += blockSize) {
        float* channelData = originalSamples.data() + offset;
        float* channels[1] = { channelData };
        AudioBlock block { channels, 1, blockSize };
        recordTrack.captureBlock(block);
    }

    recordTrack.stopRecording();

    AudioTrack playbackTrack;
    REQUIRE(playbackTrack.loadForPlayback(path.string()));

    std::vector<float> playedSamples(static_cast<std::size_t>(numFrames));
    for (int offset = 0; offset < numFrames; offset += blockSize) {
        float channelData[blockSize];
        float* channels[1] = { channelData };
        AudioBlock block { channels, 1, blockSize };
        playbackTrack.renderBlock(block, offset);

        for (int i = 0; i < blockSize; ++i) {
            playedSamples[static_cast<std::size_t>(offset + i)] = channelData[i];
        }
    }

    const double originalRms = rms(originalSamples);
    const double playedRms = rms(playedSamples);

    REQUIRE(originalRms > 0.0);
    REQUIRE(playedRms == Catch::Approx(originalRms).margin(0.01));

    std::filesystem::remove(path);
}
