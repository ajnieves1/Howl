// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Meter peak and RMS reading tests

#include "engine/Meter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using howl::AudioBlock;
using howl::engine::Meter;
using howl::engine::MeterReading;

TEST_CASE("Meter reports the correct peak for a known block", "[meter]") {
    float samples[4] = { 0.2f, -0.8f, 0.5f, -0.1f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 4 };

    Meter meter;
    meter.processBlock(block);

    MeterReading reading {};
    REQUIRE(meter.popReading(reading));
    REQUIRE(reading.peak == Catch::Approx(0.8f));
}

TEST_CASE("Meter reports the correct RMS for a known block", "[meter]") {
    float samples[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 4 };

    Meter meter;
    meter.processBlock(block);

    MeterReading reading {};
    REQUIRE(meter.popReading(reading));
    REQUIRE(reading.rms == Catch::Approx(1.0f));
}

TEST_CASE("Meter has no reading before any block is processed", "[meter]") {
    Meter meter;
    MeterReading reading {};
    REQUIRE_FALSE(meter.popReading(reading));
}
