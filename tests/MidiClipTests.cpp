// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: MidiClip note ordering, removal, and length tests

#include "model/MidiClip.h"

#include <catch2/catch_test_macros.hpp>

using hearth::model::MidiClip;
using hearth::model::Note;

TEST_CASE("MidiClip keeps notes sorted by startTick regardless of insertion order", "[model]") {
    MidiClip clip;
    clip.addNote(Note { 64, 0.8f, 960, 480 });
    clip.addNote(Note { 60, 1.0f, 0, 480 });
    clip.addNote(Note { 67, 0.6f, 480, 480 });

    const auto& notes = clip.notes();
    REQUIRE(notes.size() == 3);
    REQUIRE(notes[0].startTick == 0);
    REQUIRE(notes[1].startTick == 480);
    REQUIRE(notes[2].startTick == 960);
    REQUIRE(notes[0].key == 60);
    REQUIRE(notes[1].key == 67);
    REQUIRE(notes[2].key == 64);
}

TEST_CASE("MidiClip.removeNoteAt removes the correct note", "[model]") {
    MidiClip clip;
    clip.addNote(Note { 60, 1.0f, 0, 480 });
    clip.addNote(Note { 62, 1.0f, 480, 480 });
    clip.addNote(Note { 64, 1.0f, 960, 480 });

    clip.removeNoteAt(1);

    const auto& notes = clip.notes();
    REQUIRE(notes.size() == 2);
    REQUIRE(notes[0].key == 60);
    REQUIRE(notes[1].key == 64);
}

TEST_CASE("MidiClip.setLengthTicks and lengthTicks round-trip", "[model]") {
    MidiClip clip;
    clip.setLengthTicks(3840);
    REQUIRE(clip.lengthTicks() == 3840);
}

TEST_CASE("MidiClip.replaceNoteAt updates a note and keeps notes() sorted", "[model]") {
    MidiClip clip;
    clip.addNote(Note { 60, 1.0f, 0, 480 });
    clip.addNote(Note { 62, 1.0f, 480, 480 });
    clip.addNote(Note { 64, 1.0f, 960, 480 });

    // Move the first note (index 0, key 60) to start after the others
    const std::size_t newIndex = clip.replaceNoteAt(0, Note { 60, 1.0f, 1440, 480 });

    const auto& notes = clip.notes();
    REQUIRE(notes.size() == 3);
    REQUIRE(newIndex == 2);
    REQUIRE(notes[0].key == 62);
    REQUIRE(notes[1].key == 64);
    REQUIRE(notes[2].key == 60);
    REQUIRE(notes[2].startTick == 1440);
}
