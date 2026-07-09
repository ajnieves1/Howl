// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: CommandStack undo, redo, and concrete command round-trip tests

#include "model/Arrangement.h"
#include "model/AudioClip.h"
#include "model/CommandStack.h"
#include "model/Commands.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using howl::model::AddMidiClipCommand;
using howl::model::Arrangement;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::CommandStack;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::MoveAudioClipCommand;
using howl::model::MoveMidiClipCommand;
using howl::model::Note;
using howl::model::RemoveMidiClipCommand;
using howl::model::TrackKind;

TEST_CASE("CommandStack performs, undoes, and redoes an add-clip command", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clip;
    clip.addNote(Note { 60, 1.0f, 0, 480 });

    CommandStack stack;
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());

    stack.perform(std::make_unique<AddMidiClipCommand>(arrangement, trackIndex, MidiClipPlacement { 0, clip }));

    REQUIRE(arrangement.track(trackIndex).midiClips.size() == 1);
    REQUIRE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());

    stack.undo();
    REQUIRE(arrangement.track(trackIndex).midiClips.empty());
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE(stack.canRedo());

    stack.redo();
    REQUIRE(arrangement.track(trackIndex).midiClips.size() == 1);
    REQUIRE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("CommandStack.perform after an undo clears the redo stack", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clipA;
    clipA.addNote(Note { 60, 1.0f, 0, 480 });
    MidiClip clipB;
    clipB.addNote(Note { 64, 1.0f, 0, 480 });

    CommandStack stack;
    stack.perform(std::make_unique<AddMidiClipCommand>(arrangement, trackIndex, MidiClipPlacement { 0, clipA }));
    stack.undo();
    REQUIRE(stack.canRedo());

    stack.perform(std::make_unique<AddMidiClipCommand>(arrangement, trackIndex, MidiClipPlacement { 960, clipB }));

    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(arrangement.track(trackIndex).midiClips.size() == 1);
    REQUIRE(arrangement.track(trackIndex).midiClips[0].startTick == 960);
}

TEST_CASE("RemoveMidiClipCommand removes a placement and undo restores it", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clip;
    clip.addNote(Note { 60, 1.0f, 0, 480 });
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, clip });

    CommandStack stack;
    stack.perform(std::make_unique<RemoveMidiClipCommand>(arrangement, trackIndex, 0));

    REQUIRE(arrangement.track(trackIndex).midiClips.empty());

    stack.undo();
    REQUIRE(arrangement.track(trackIndex).midiClips.size() == 1);
    REQUIRE(arrangement.track(trackIndex).midiClips[0].startTick == 0);
}

TEST_CASE("MoveMidiClipCommand changes a placement's start tick and undo restores it", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clip;
    clip.addNote(Note { 60, 1.0f, 0, 480 });
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, clip });

    CommandStack stack;
    stack.perform(std::make_unique<MoveMidiClipCommand>(arrangement, trackIndex, 0, 1920));

    REQUIRE(arrangement.track(trackIndex).midiClips[0].startTick == 1920);

    stack.undo();
    REQUIRE(arrangement.track(trackIndex).midiClips[0].startTick == 0);

    stack.redo();
    REQUIRE(arrangement.track(trackIndex).midiClips[0].startTick == 1920);
}

TEST_CASE("MoveAudioClipCommand changes a placement's start tick and undo restores it", "[model]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Vocals", TrackKind::Audio);

    AudioClip clip(std::vector<std::vector<float>> { { 0.1f, 0.2f } }, 44100.0);
    arrangement.addAudioClipPlacement(trackIndex, AudioClipPlacement { 0, clip });

    CommandStack stack;
    stack.perform(std::make_unique<MoveAudioClipCommand>(arrangement, trackIndex, 0, 1920));

    REQUIRE(arrangement.track(trackIndex).audioClips[0].startTick == 1920);

    stack.undo();
    REQUIRE(arrangement.track(trackIndex).audioClips[0].startTick == 0);

    stack.redo();
    REQUIRE(arrangement.track(trackIndex).audioClips[0].startTick == 1920);
}

// AddNoteCommand and RemoveNoteCommand moved to NoteCommandTests.cpp, they now take a
// ClipAddress instead of a raw placement index (P9-T2)
