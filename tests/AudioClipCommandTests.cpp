// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AddAudioClipCommand/RemoveAudioClipCommand round-trip tests

#include "model/Arrangement.h"
#include "model/Commands.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::model::AddAudioClipCommand;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::Arrangement;
using howl::model::RemoveAudioClipCommand;
using howl::model::TrackKind;

namespace {

// Builds a tiny one-channel AudioClip with a source path set, for round-trip checks
AudioClip makeTestClip() {
    AudioClip clip(std::vector<std::vector<float>> { { 1.0f, 2.0f, 3.0f } }, 44100.0);
    clip.setSourcePath("/tmp/test.wav");
    return clip;
}

} // namespace

TEST_CASE("AddAudioClipCommand execute adds the placement, undo removes it", "[audioclipcommand]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Audio 1", TrackKind::Audio);

    AudioClipPlacement placement { 480, makeTestClip() };
    AddAudioClipCommand command(arrangement, trackIndex, placement);

    REQUIRE(arrangement.track(trackIndex).audioClips.empty());

    command.execute();
    REQUIRE(arrangement.track(trackIndex).audioClips.size() == 1);
    REQUIRE(arrangement.track(trackIndex).audioClips[0].startTick == 480);
    REQUIRE(arrangement.track(trackIndex).audioClips[0].clip.sourcePath() == "/tmp/test.wav");

    command.undo();
    REQUIRE(arrangement.track(trackIndex).audioClips.empty());
}

TEST_CASE("RemoveAudioClipCommand undo restores the exact placement, startTick and sourcePath intact", "[audioclipcommand]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Audio 1", TrackKind::Audio);
    arrangement.addAudioClipPlacement(trackIndex, AudioClipPlacement { 960, makeTestClip() });

    RemoveAudioClipCommand command(arrangement, trackIndex, 0);
    command.execute();
    REQUIRE(arrangement.track(trackIndex).audioClips.empty());

    command.undo();
    REQUIRE(arrangement.track(trackIndex).audioClips.size() == 1);
    REQUIRE(arrangement.track(trackIndex).audioClips[0].startTick == 960);
    REQUIRE(arrangement.track(trackIndex).audioClips[0].clip.sourcePath() == "/tmp/test.wav");
}

TEST_CASE("AddAudioClipCommand redo (execute after undo) lands the placement again", "[audioclipcommand]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Audio 1", TrackKind::Audio);

    AudioClipPlacement placement { 0, makeTestClip() };
    AddAudioClipCommand command(arrangement, trackIndex, placement);

    command.execute();
    command.undo();
    command.execute();

    REQUIRE(arrangement.track(trackIndex).audioClips.size() == 1);
    REQUIRE(arrangement.track(trackIndex).audioClips[0].startTick == 0);
}
