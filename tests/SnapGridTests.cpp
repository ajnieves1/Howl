// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: SnapGrid's pure tick rounding for every division

#include "model/SnapGrid.h"

#include <catch2/catch_test_macros.hpp>

using howl::model::SnapDivision;
using howl::model::snapTick;
using howl::model::snapTickFloor;
using howl::model::snapUnitTicks;

TEST_CASE("SnapGrid.snapUnitTicks returns the fixed tick size for each division", "[snapgrid]") {
    REQUIRE(snapUnitTicks(SnapDivision::Bar) == 3840);
    REQUIRE(snapUnitTicks(SnapDivision::Beat) == 960);
    REQUIRE(snapUnitTicks(SnapDivision::HalfBeat) == 480);
    REQUIRE(snapUnitTicks(SnapDivision::Step) == 240);
    REQUIRE(snapUnitTicks(SnapDivision::Off) == 0);
}

TEST_CASE("SnapGrid.snapTick rounds to the nearest unit, a midpoint rounds up", "[snapgrid]") {
    REQUIRE(snapTick(0, SnapDivision::Beat) == 0);
    REQUIRE(snapTick(479, SnapDivision::Beat) == 0);
    REQUIRE(snapTick(480, SnapDivision::Beat) == 960);
    REQUIRE(snapTick(960, SnapDivision::Beat) == 960);
    REQUIRE(snapTick(1439, SnapDivision::Beat) == 960);
    REQUIRE(snapTick(1440, SnapDivision::Beat) == 1920);
}

TEST_CASE("SnapGrid.snapTick rounds correctly for every non Off division", "[snapgrid]") {
    REQUIRE(snapTick(2000, SnapDivision::Bar) == 3840);
    REQUIRE(snapTick(1000, SnapDivision::Bar) == 0);
    REQUIRE(snapTick(300, SnapDivision::HalfBeat) == 480);
    REQUIRE(snapTick(150, SnapDivision::HalfBeat) == 0);
    REQUIRE(snapTick(200, SnapDivision::Step) == 240);
    REQUIRE(snapTick(50, SnapDivision::Step) == 0);
}

TEST_CASE("SnapGrid.snapTick clamps negative results at 0", "[snapgrid]") {
    REQUIRE(snapTick(-100, SnapDivision::Beat) == 0);
    REQUIRE(snapTick(-1000, SnapDivision::Beat) == 0);
}

TEST_CASE("SnapGrid.snapTick leaves the tick unchanged for Off, even negative", "[snapgrid]") {
    REQUIRE(snapTick(12345, SnapDivision::Off) == 12345);
    REQUIRE(snapTick(-500, SnapDivision::Off) == -500);
}

TEST_CASE("SnapGrid.snapTickFloor rounds down to the unit at or below", "[snapgrid]") {
    REQUIRE(snapTickFloor(0, SnapDivision::Beat) == 0);
    REQUIRE(snapTickFloor(959, SnapDivision::Beat) == 0);
    REQUIRE(snapTickFloor(960, SnapDivision::Beat) == 960);
    REQUIRE(snapTickFloor(1919, SnapDivision::Beat) == 960);
}

TEST_CASE("SnapGrid.snapTickFloor clamps negative results at 0", "[snapgrid]") {
    REQUIRE(snapTickFloor(-1, SnapDivision::Beat) == 0);
    REQUIRE(snapTickFloor(-5000, SnapDivision::Bar) == 0);
}

TEST_CASE("SnapGrid.snapTickFloor leaves the tick unchanged for Off, even negative", "[snapgrid]") {
    REQUIRE(snapTickFloor(12345, SnapDivision::Off) == 12345);
    REQUIRE(snapTickFloor(-500, SnapDivision::Off) == -500);
}
