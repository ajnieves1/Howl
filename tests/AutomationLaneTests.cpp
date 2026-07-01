// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AutomationLane interpolation, clamping, and ordering tests

#include "model/AutomationLane.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using howl::model::AutomationLane;
using howl::model::AutomationPoint;

TEST_CASE("AutomationLane returns 0.5 when empty", "[model]") {
    AutomationLane lane;
    REQUIRE(lane.valueAtTick(0) == Catch::Approx(0.5f));
}

TEST_CASE("AutomationLane clamps to the first and last point outside their range", "[model]") {
    AutomationLane lane;
    lane.addPoint(AutomationPoint { 100, 0.2f });
    lane.addPoint(AutomationPoint { 200, 0.8f });

    REQUIRE(lane.valueAtTick(0) == Catch::Approx(0.2f));
    REQUIRE(lane.valueAtTick(100) == Catch::Approx(0.2f));
    REQUIRE(lane.valueAtTick(200) == Catch::Approx(0.8f));
    REQUIRE(lane.valueAtTick(1000) == Catch::Approx(0.8f));
}

TEST_CASE("AutomationLane linearly interpolates between two bracketing points", "[model]") {
    AutomationLane lane;
    lane.addPoint(AutomationPoint { 0, 0.0f });
    lane.addPoint(AutomationPoint { 100, 1.0f });

    REQUIRE(lane.valueAtTick(25) == Catch::Approx(0.25f));
    REQUIRE(lane.valueAtTick(50) == Catch::Approx(0.5f));
    REQUIRE(lane.valueAtTick(75) == Catch::Approx(0.75f));
}

TEST_CASE("AutomationLane.removePointAt removes the correct point", "[model]") {
    AutomationLane lane;
    lane.addPoint(AutomationPoint { 0, 0.0f });
    lane.addPoint(AutomationPoint { 100, 0.5f });
    lane.addPoint(AutomationPoint { 200, 1.0f });

    lane.removePointAt(1);

    const auto& points = lane.points();
    REQUIRE(points.size() == 2);
    REQUIRE(points[0].tick == 0);
    REQUIRE(points[1].tick == 200);
}

TEST_CASE("AutomationLane keeps points sorted by tick regardless of insertion order", "[model]") {
    AutomationLane lane;
    lane.addPoint(AutomationPoint { 200, 1.0f });
    lane.addPoint(AutomationPoint { 0, 0.0f });
    lane.addPoint(AutomationPoint { 100, 0.5f });

    const auto& points = lane.points();
    REQUIRE(points.size() == 3);
    REQUIRE(points[0].tick == 0);
    REQUIRE(points[1].tick == 100);
    REQUIRE(points[2].tick == 200);
}
