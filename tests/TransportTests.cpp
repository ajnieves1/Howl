// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: Transport playhead and loop-wrap tests

#include "engine/Transport.h"

#include <catch2/catch_test_macros.hpp>

using hearth::SampleCount;
using hearth::engine::Transport;

TEST_CASE("Transport advances the playhead sample-exactly", "[transport]") {
    Transport transport;
    transport.play();

    const SampleCount firstBlockStart = transport.advance(512);
    REQUIRE(firstBlockStart == 0);

    const SampleCount secondBlockStart = transport.advance(512);
    REQUIRE(secondBlockStart == 512);

    REQUIRE(transport.position() == 1024);
}

TEST_CASE("Transport does not advance while stopped", "[transport]") {
    Transport transport;

    transport.advance(512);
    REQUIRE(transport.position() == 0);
}

TEST_CASE("Transport wraps at the loop end, sample-exact", "[transport]") {
    Transport transport;
    transport.setLoop(100, 200, true);
    transport.play();

    const SampleCount blockStart = transport.advance(100);
    REQUIRE(blockStart == 0);
    REQUIRE(transport.position() == 100);

    const SampleCount wrapBlockStart = transport.advance(150);
    REQUIRE(wrapBlockStart == 100);
    REQUIRE(transport.position() == 150);
}

TEST_CASE("Transport wraps multiple times within a single block", "[transport]") {
    Transport transport;
    transport.setLoop(0, 50, true);
    transport.play();

    const SampleCount blockStart = transport.advance(130);
    REQUIRE(blockStart == 0);
    REQUIRE(transport.position() == 30);
}

TEST_CASE("Transport.isPlaying reflects play and stop", "[transport]") {
    Transport transport;
    REQUIRE_FALSE(transport.isPlaying());

    transport.play();
    REQUIRE(transport.isPlaying());

    transport.stop();
    REQUIRE_FALSE(transport.isPlaying());
}
