// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AudioTrackRenderer clip-placement position test

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/AudioTrackRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::engine::Transport;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::AudioTrackRenderer;
using howl::model::kTicksPerQuarter;
using howl::model::Track;
using howl::model::TrackKind;

TEST_CASE("AudioTrackRenderer places clip samples at the correct timeline position", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 32768;

    AudioClip clip(std::vector<std::vector<float>> { { 1.0f, 2.0f, 3.0f, 4.0f } }, sampleRate);

    Track track;
    track.name = "Vocals";
    track.kind = TrackKind::Audio;
    track.audioClips.push_back(AudioClipPlacement { kTicksPerQuarter, clip }); // placed one beat in

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    AudioTrackRenderer renderer(transport, track);
    renderer.prepare(sampleRate);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };

    const SampleCount pos = transport.advance(blockSize);
    REQUIRE(pos == 0);

    AudioBlock block { channels, 1, blockSize };
    renderer.process(block, pos);

    // 120 bpm at 44100 Hz: one beat (960 ticks) is 22050 samples exactly
    const int expectedOffset = 22050;

    REQUIRE(buffer[static_cast<std::size_t>(expectedOffset) - 1] == 0.0f);
    REQUIRE(buffer[static_cast<std::size_t>(expectedOffset)] == 1.0f);
    REQUIRE(buffer[static_cast<std::size_t>(expectedOffset) + 1] == 2.0f);
    REQUIRE(buffer[static_cast<std::size_t>(expectedOffset) + 2] == 3.0f);
    REQUIRE(buffer[static_cast<std::size_t>(expectedOffset) + 3] == 4.0f);
    REQUIRE(buffer[static_cast<std::size_t>(expectedOffset) + 4] == 0.0f);
}
