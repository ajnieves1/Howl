// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: PreviewPlayer's RT-safe post/process/garbage-collect handoff

#include "model/PreviewPlayer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <vector>

using howl::AudioBlock;
using howl::model::PreviewBuffer;
using howl::model::PreviewPlayer;

namespace {

constexpr int kNumChannels = 2;
constexpr int kBlockSize = 8;

// Builds a two channel preview buffer of the given length, each sample an ascending ramp
std::unique_ptr<PreviewBuffer> makeRampBuffer(int length, float startValue) {
    auto buffer = std::make_unique<PreviewBuffer>();
    buffer->channels.assign(kNumChannels, std::vector<float>(static_cast<std::size_t>(length)));

    for (int channel = 0; channel < kNumChannels; ++channel) {
        for (int frame = 0; frame < length; ++frame) {
            buffer->channels[static_cast<std::size_t>(channel)][static_cast<std::size_t>(frame)]
                = startValue + static_cast<float>(frame);
        }
    }

    return buffer;
}

} // namespace

TEST_CASE("PreviewPlayer mixes a posted ramp additively over existing block content", "[previewplayer]") {
    PreviewPlayer player;
    player.post(makeRampBuffer(kBlockSize, 0.0f));

    float left[kBlockSize];
    float right[kBlockSize];
    for (int i = 0; i < kBlockSize; ++i) {
        left[i] = 1.0f;
        right[i] = 2.0f;
    }
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    player.process(block);

    for (int i = 0; i < kBlockSize; ++i) {
        REQUIRE(left[i] == Catch::Approx(1.0f + static_cast<float>(i)));
        REQUIRE(right[i] == Catch::Approx(2.0f + static_cast<float>(i)));
    }
}

TEST_CASE("PreviewPlayer swaps cleanly when a second buffer is posted mid play", "[previewplayer]") {
    PreviewPlayer player;
    player.post(makeRampBuffer(kBlockSize * 4, 0.0f));

    float left[kBlockSize] {};
    float right[kBlockSize] {};
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    // Plays the first quarter of the long ramp, leaving it mid playback
    player.process(block);

    // Displaces the long ramp before it finishes
    player.post(makeRampBuffer(kBlockSize, 100.0f));

    for (int i = 0; i < kBlockSize; ++i) {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }
    player.process(block);

    // The swap resets playback position, this block plays the new buffer from its own start
    for (int i = 0; i < kBlockSize; ++i) {
        REQUIRE(left[i] == Catch::Approx(100.0f + static_cast<float>(i)));
    }

    // Frees the displaced long ramp, must not crash or double free
    player.collectGarbage();
}

TEST_CASE("PreviewPlayer.stop silences the next block", "[previewplayer]") {
    PreviewPlayer player;
    player.post(makeRampBuffer(kBlockSize * 4, 0.0f));

    float left[kBlockSize] {};
    float right[kBlockSize] {};
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    player.process(block); // starts playing
    player.stop();

    for (int i = 0; i < kBlockSize; ++i) {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }
    player.process(block);

    for (int i = 0; i < kBlockSize; ++i) {
        REQUIRE(left[i] == 0.0f);
        REQUIRE(right[i] == 0.0f);
    }
}

TEST_CASE("PreviewPlayer retires a finished buffer and collectGarbage frees it without crashing", "[previewplayer]") {
    PreviewPlayer player;
    player.post(makeRampBuffer(kBlockSize, 0.0f)); // exactly one block long

    float left[kBlockSize] {};
    float right[kBlockSize] {};
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    player.process(block); // consumes the whole buffer, it retires at the end of this call
    player.collectGarbage();

    for (int i = 0; i < kBlockSize; ++i) {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }
    player.process(block); // nothing active, must stay silent

    for (int i = 0; i < kBlockSize; ++i) {
        REQUIRE(left[i] == 0.0f);
        REQUIRE(right[i] == 0.0f);
    }
}
