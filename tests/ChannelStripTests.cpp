// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: ChannelStrip gain, pan, and mute tests

#include "model/ChannelStrip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using howl::AudioBlock;
using howl::model::ChannelStrip;

TEST_CASE("ChannelStrip at default settings is a bit-exact passthrough", "[channelstrip]") {
    float samples[4] = { 1.0f, 2.0f, -1.0f, 0.5f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 4 };

    ChannelStrip strip;
    strip.process(block);

    REQUIRE(samples[0] == 1.0f);
    REQUIRE(samples[1] == 2.0f);
    REQUIRE(samples[2] == -1.0f);
    REQUIRE(samples[3] == 0.5f);
}

TEST_CASE("ChannelStrip at -6 dB halves amplitude within tolerance", "[channelstrip]") {
    float left[1] = { 1.0f };
    float right[1] = { 1.0f };
    float* channels[2] = { left, right };
    AudioBlock block { channels, 2, 1 };

    ChannelStrip strip;
    strip.setGainDb(-6.0f);
    strip.process(block);

    REQUIRE(left[0] == Catch::Approx(0.5f).margin(0.01f));
    REQUIRE(right[0] == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("ChannelStrip hard-left pan zeroes the right channel and leaves left unity", "[channelstrip]") {
    float left[1] = { 1.0f };
    float right[1] = { 1.0f };
    float* channels[2] = { left, right };
    AudioBlock block { channels, 2, 1 };

    ChannelStrip strip;
    strip.setPan(-1.0f);
    strip.process(block);

    REQUIRE(left[0] == 1.0f);
    REQUIRE(right[0] == 0.0f);
}

TEST_CASE("ChannelStrip hard-right pan zeroes the left channel and leaves right unity", "[channelstrip]") {
    float left[1] = { 1.0f };
    float right[1] = { 1.0f };
    float* channels[2] = { left, right };
    AudioBlock block { channels, 2, 1 };

    ChannelStrip strip;
    strip.setPan(1.0f);
    strip.process(block);

    REQUIRE(left[0] == 0.0f);
    REQUIRE(right[0] == 1.0f);
}

TEST_CASE("ChannelStrip when muted silences the block", "[channelstrip]") {
    float samples[3] = { 1.0f, 2.0f, 3.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 3 };

    ChannelStrip strip;
    strip.setMuted(true);
    strip.process(block);

    REQUIRE(samples[0] == 0.0f);
    REQUIRE(samples[1] == 0.0f);
    REQUIRE(samples[2] == 0.0f);
}

TEST_CASE("ChannelStrip accessors round-trip gain, pan, mute, and solo", "[channelstrip]") {
    ChannelStrip strip;

    strip.setGainDb(-3.0f);
    strip.setPan(0.25f);
    strip.setMuted(true);
    strip.setSoloed(true);

    REQUIRE(strip.gainDb() == Catch::Approx(-3.0f));
    REQUIRE(strip.pan() == Catch::Approx(0.25f));
    REQUIRE(strip.muted());
    REQUIRE(strip.soloed());
}
