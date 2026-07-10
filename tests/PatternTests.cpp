// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: PatternBank columns, placements, resolveClip's Pattern case, and patternLengthTicks

#include "model/Arrangement.h"
#include "model/Commands.h"
#include "model/Mixer.h"
#include "model/Note.h"
#include "model/Pattern.h"
#include "model/Session.h"

#include <catch2/catch_test_macros.hpp>

using howl::model::AddPatternPlacementCommand;
using howl::model::AddTrackCommand;
using howl::model::Arrangement;
using howl::model::ClipAddress;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::Mixer;
using howl::model::MovePatternPlacementCommand;
using howl::model::Pattern;
using howl::model::PatternBank;
using howl::model::PatternPlacement;
using howl::model::RemovePatternPlacementCommand;
using howl::model::RemoveTrackCommand;
using howl::model::resolveClip;
using howl::model::Session;
using howl::model::TrackKind;

TEST_CASE("PatternBank's columns grow and shrink with the track commands and round trip through undo", "[pattern]") {
    Arrangement arrangement;
    Mixer mixer;
    Session session;
    PatternBank patterns;
    patterns.addPattern("Pattern 1", 0);

    AddTrackCommand addCommand(arrangement, mixer, session, patterns, "Lead", TrackKind::Midi);
    addCommand.execute();
    REQUIRE(patterns.pattern(0).trackClips.size() == 1);

    addCommand.undo();
    REQUIRE(patterns.pattern(0).trackClips.empty());

    addCommand.execute();
    const std::size_t trackIndex = 0;
    RemoveTrackCommand removeCommand(arrangement, mixer, session, patterns, trackIndex);
    removeCommand.execute();
    REQUIRE(patterns.pattern(0).trackClips.empty());

    removeCommand.undo();
    REQUIRE(patterns.pattern(0).trackClips.size() == 1);
}

TEST_CASE("Pattern placement commands round trip through execute and undo", "[pattern]") {
    PatternBank patterns;
    patterns.addPattern("Pattern 1", 1);

    AddPatternPlacementCommand addCommand(patterns, PatternPlacement { 0, kTicksPerQuarter * 4 });
    addCommand.execute();
    REQUIRE(patterns.placements().size() == 1);
    REQUIRE(patterns.placements()[0].startTick == kTicksPerQuarter * 4);

    addCommand.undo();
    REQUIRE(patterns.placements().empty());

    addCommand.execute();
    MovePatternPlacementCommand moveCommand(patterns, 0, kTicksPerQuarter * 4, kTicksPerQuarter * 8);
    moveCommand.execute();
    REQUIRE(patterns.placements()[0].startTick == kTicksPerQuarter * 8);

    moveCommand.undo();
    REQUIRE(patterns.placements()[0].startTick == kTicksPerQuarter * 4);

    RemovePatternPlacementCommand removeCommand(patterns, 0);
    removeCommand.execute();
    REQUIRE(patterns.placements().empty());

    removeCommand.undo();
    REQUIRE(patterns.placements().size() == 1);
    REQUIRE(patterns.placements()[0].startTick == kTicksPerQuarter * 4);
}

TEST_CASE("resolveClip resolves a pattern lane and bounds checks", "[pattern]") {
    Arrangement arrangement;
    Session session;
    PatternBank patterns;
    patterns.addPattern("Pattern 1", 2);

    const ClipAddress address { ClipAddress::Source::Pattern, 1, 0 };
    MidiClip* clip = resolveClip(arrangement, session, &patterns, address);
    REQUIRE(clip == &patterns.pattern(0).trackClips[1]);

    const ClipAddress badTrack { ClipAddress::Source::Pattern, 5, 0 };
    REQUIRE(resolveClip(arrangement, session, &patterns, badTrack) == nullptr);

    const ClipAddress badPattern { ClipAddress::Source::Pattern, 0, 5 };
    REQUIRE(resolveClip(arrangement, session, &patterns, badPattern) == nullptr);

    REQUIRE(resolveClip(arrangement, session, nullptr, address) == nullptr);
}

TEST_CASE("patternLengthTicks floors at one bar and tracks the longest lane", "[pattern]") {
    PatternBank patterns;
    patterns.addPattern("Pattern 1", 2);

    REQUIRE(patterns.patternLengthTicks(0) == kTicksPerQuarter * 4);

    patterns.pattern(0).trackClips[1].setLengthTicks(kTicksPerQuarter * 2);
    REQUIRE(patterns.patternLengthTicks(0) == kTicksPerQuarter * 4);

    patterns.pattern(0).trackClips[0].setLengthTicks(kTicksPerQuarter * 16);
    REQUIRE(patterns.patternLengthTicks(0) == kTicksPerQuarter * 16);
}
