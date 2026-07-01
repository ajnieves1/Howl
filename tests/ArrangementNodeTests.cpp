// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: ArrangementNode multi-track summing test

#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::engine::Transport;
using howl::model::Arrangement;
using howl::model::ArrangementNode;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::TrackKind;

TEST_CASE("ArrangementNode sums two audio tracks placed at the same position", "[model]") {
    const double sampleRate = 44100.0;
    const int blockSize = 512;

    Arrangement arrangement;
    const std::size_t trackA = arrangement.addTrack("A", TrackKind::Audio);
    const std::size_t trackB = arrangement.addTrack("B", TrackKind::Audio);

    AudioClip clipA(std::vector<std::vector<float>> { { 0.25f, 0.25f, 0.25f } }, sampleRate);
    AudioClip clipB(std::vector<std::vector<float>> { { 0.5f, 0.5f, 0.5f } }, sampleRate);

    arrangement.addAudioClipPlacement(trackA, AudioClipPlacement { 0, clipA });
    arrangement.addAudioClipPlacement(trackB, AudioClipPlacement { 0, clipB });

    Transport transport;
    transport.play();

    ArrangementNode node(transport, arrangement);
    node.prepare(sampleRate, blockSize, 1);

    std::vector<float> buffer(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[1] = { buffer.data() };

    const SampleCount pos = transport.advance(blockSize);
    REQUIRE(pos == 0);

    AudioBlock block { channels, 1, blockSize };
    node.process(block, pos);

    REQUIRE(buffer[0] == 0.75f);
    REQUIRE(buffer[1] == 0.75f);
    REQUIRE(buffer[2] == 0.75f);
    REQUIRE(buffer[3] == 0.0f);
}
