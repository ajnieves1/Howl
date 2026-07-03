// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AudioClip warp fields and active-buffer accessors

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/AudioClip.h"
#include "model/AudioTrackRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::engine::Transport;
using howl::model::Arrangement;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::AudioTrackRenderer;
using howl::model::Track;
using howl::model::TrackKind;

TEST_CASE("AudioClip active accessors return source data by default", "[audioclip]") {
    AudioClip clip(std::vector<std::vector<float>> { { 1.0f, 2.0f, 3.0f } }, 44100.0);

    REQUIRE(clip.activeLengthSamples() == 3);
    REQUIRE(clip.activeChannelData(0)[0] == 1.0f);
    REQUIRE(clip.activeChannelData(0)[2] == 3.0f);
    REQUIRE(clip.warpEnabled() == false);
    REQUIRE(clip.warpedTempo() == 0.0);
}

TEST_CASE("AudioClip active accessors switch to warped data once enabled and installed", "[audioclip]") {
    AudioClip clip(std::vector<std::vector<float>> { { 1.0f, 2.0f, 3.0f } }, 44100.0);

    clip.setWarpedChannels(std::vector<std::vector<float>> { { 9.0f, 8.0f, 7.0f, 6.0f } }, 130.0);
    clip.setWarpEnabled(true);

    REQUIRE(clip.activeLengthSamples() == 4);
    REQUIRE(clip.activeChannelData(0)[0] == 9.0f);
    REQUIRE(clip.warpedTempo() == 130.0);

    // Disabling warp falls back to source, even with a warped buffer still installed
    clip.setWarpEnabled(false);
    REQUIRE(clip.activeLengthSamples() == 3);
    REQUIRE(clip.activeChannelData(0)[0] == 1.0f);

    // Clearing the warped buffer falls back to source even with warp enabled
    clip.setWarpEnabled(true);
    clip.clearWarpedChannels();
    REQUIRE(clip.activeLengthSamples() == 3);
    REQUIRE(clip.activeChannelData(0)[0] == 1.0f);
    REQUIRE(clip.warpedTempo() == 0.0);
}

TEST_CASE("AudioClip originalBpm round-trips", "[audioclip]") {
    AudioClip clip;
    REQUIRE(clip.originalBpm() == 0.0);
    clip.setOriginalBpm(128.0);
    REQUIRE(clip.originalBpm() == 128.0);
}

TEST_CASE("AudioTrackRenderer renders warped samples when a clip's warp is enabled", "[audioclip]") {
    const double sampleRate = 44100.0;
    const int blockSize = 16;

    AudioClip clip(std::vector<std::vector<float>> { { 0.1f, 0.1f, 0.1f, 0.1f } }, sampleRate);
    clip.setWarpedChannels(std::vector<std::vector<float>> { { 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f } }, 90.0);
    clip.setWarpEnabled(true);

    Track track;
    track.name = "Audio";
    track.kind = TrackKind::Audio;
    track.audioClips.push_back(AudioClipPlacement { 0, clip });

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    AudioTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };
    AudioBlock block { channels, 1, blockSize };

    const SampleCount pos = transport.advance(blockSize);
    renderer.process(block, pos);

    // The warped (0.9) samples should appear, not the source (0.1) samples
    REQUIRE(buffer[0] == 0.9f);
    REQUIRE(buffer[5] == 0.9f);
    REQUIRE(buffer[6] == 0.0f); // past the warped clip's length
}
