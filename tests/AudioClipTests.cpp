// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AudioClip channel and length round-trip tests

#include "model/AudioClip.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::model::AudioClip;

TEST_CASE("AudioClip reports channel count, length, and sample rate", "[model]") {
    std::vector<std::vector<float>> channels = {
        { 0.1f, 0.2f, 0.3f },
        { -0.1f, -0.2f, -0.3f }
    };

    AudioClip clip(channels, 48000.0);

    REQUIRE(clip.numChannels() == 2);
    REQUIRE(clip.lengthSamples() == 3);
    REQUIRE(clip.sourceSampleRate() == 48000.0);
}

TEST_CASE("AudioClip.channelData exposes the original samples per channel", "[model]") {
    std::vector<std::vector<float>> channels = {
        { 1.0f, 2.0f, 3.0f },
        { 4.0f, 5.0f, 6.0f }
    };

    AudioClip clip(channels, 44100.0);

    const float* left = clip.channelData(0);
    const float* right = clip.channelData(1);

    REQUIRE(left[0] == 1.0f);
    REQUIRE(left[1] == 2.0f);
    REQUIRE(left[2] == 3.0f);
    REQUIRE(right[0] == 4.0f);
    REQUIRE(right[1] == 5.0f);
    REQUIRE(right[2] == 6.0f);
}
