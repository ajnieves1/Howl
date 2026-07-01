// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: Arrangement track and clip placement ordering tests

#include "model/Arrangement.h"

#include <catch2/catch_test_macros.hpp>

using hearth::model::Arrangement;
using hearth::model::AudioClip;
using hearth::model::AudioClipPlacement;
using hearth::model::MidiClip;
using hearth::model::MidiClipPlacement;
using hearth::model::Note;
using hearth::model::TrackKind;

TEST_CASE("Arrangement adds tracks of each kind and returns their index", "[model]") {
    Arrangement arrangement;

    const std::size_t midiIndex = arrangement.addTrack("Lead", TrackKind::Midi);
    const std::size_t audioIndex = arrangement.addTrack("Vocals", TrackKind::Audio);

    REQUIRE(arrangement.numTracks() == 2);
    REQUIRE(midiIndex == 0);
    REQUIRE(audioIndex == 1);
    REQUIRE(arrangement.track(midiIndex).name == "Lead");
    REQUIRE(arrangement.track(midiIndex).kind == TrackKind::Midi);
    REQUIRE(arrangement.track(audioIndex).name == "Vocals");
    REQUIRE(arrangement.track(audioIndex).kind == TrackKind::Audio);
}

TEST_CASE("Arrangement keeps MIDI clip placements sorted by startTick", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clipA;
    clipA.addNote(Note { 60, 1.0f, 0, 480 });

    MidiClip clipB;
    clipB.addNote(Note { 64, 1.0f, 0, 480 });

    MidiClip clipC;
    clipC.addNote(Note { 67, 1.0f, 0, 480 });

    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 1920, clipA });
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, clipB });
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 960, clipC });

    const auto& placements = arrangement.track(trackIndex).midiClips;
    REQUIRE(placements.size() == 3);
    REQUIRE(placements[0].startTick == 0);
    REQUIRE(placements[1].startTick == 960);
    REQUIRE(placements[2].startTick == 1920);
}

TEST_CASE("Arrangement keeps audio clip placements sorted by startTick", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Vocals", TrackKind::Audio);

    AudioClip clipA(std::vector<std::vector<float>> { { 0.1f, 0.2f } }, 44100.0);
    AudioClip clipB(std::vector<std::vector<float>> { { 0.3f, 0.4f } }, 44100.0);

    arrangement.addAudioClipPlacement(trackIndex, AudioClipPlacement { 4800, clipA });
    arrangement.addAudioClipPlacement(trackIndex, AudioClipPlacement { 0, clipB });

    const auto& placements = arrangement.track(trackIndex).audioClips;
    REQUIRE(placements.size() == 2);
    REQUIRE(placements[0].startTick == 0);
    REQUIRE(placements[1].startTick == 4800);
}
