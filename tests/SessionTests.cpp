// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: Session grid bookkeeping and slot commands

#include "model/Commands.h"
#include "model/Session.h"

#include <catch2/catch_test_macros.hpp>

using howl::model::AddSessionAudioClipCommand;
using howl::model::AddSessionMidiClipCommand;
using howl::model::AudioClip;
using howl::model::ClearSessionSlotCommand;
using howl::model::MidiClip;
using howl::model::Session;
using howl::model::SlotContent;

TEST_CASE("Session::addScene grows every existing column", "[session]") {
    Session session;
    session.addTrackColumn();
    session.addTrackColumn();

    REQUIRE(session.numScenes() == 0);
    const std::size_t sceneIndex = session.addScene();
    REQUIRE(sceneIndex == 0);
    REQUIRE(session.numScenes() == 1);
    REQUIRE(session.slot(0, 0).content == SlotContent::Empty);
    REQUIRE(session.slot(1, 0).content == SlotContent::Empty);

    session.addScene();
    REQUIRE(session.numScenes() == 2);
}

TEST_CASE("Session::addTrackColumn sizes the new column to the current scene count", "[session]") {
    Session session;
    session.addTrackColumn();
    session.addScene();
    session.addScene();

    session.addTrackColumn();
    REQUIRE(session.numTracks() == 2);
    REQUIRE(session.slot(1, 0).content == SlotContent::Empty);
    REQUIRE(session.slot(1, 1).content == SlotContent::Empty);
}

TEST_CASE("Session::insertTrackColumn and removeTrackColumn round-trip a column at an interior index", "[session]") {
    Session session;
    session.addTrackColumn();
    session.addTrackColumn();
    session.addTrackColumn();
    session.addScene();

    session.slot(1, 0).content = SlotContent::Midi;

    std::vector<howl::model::ClipSlot> removed = session.removeTrackColumn(1);
    REQUIRE(session.numTracks() == 2);
    REQUIRE(removed.size() == 1);
    REQUIRE(removed[0].content == SlotContent::Midi);

    session.insertTrackColumn(1, removed);
    REQUIRE(session.numTracks() == 3);
    REQUIRE(session.slot(1, 0).content == SlotContent::Midi);
}

TEST_CASE("AddSessionMidiClipCommand execute fills an empty slot, undo empties it again", "[session]") {
    Session session;
    session.addTrackColumn();
    session.addScene();

    MidiClip clip;
    clip.setLengthTicks(960);

    AddSessionMidiClipCommand command(session, 0, 0, clip);
    command.execute();

    REQUIRE(session.slot(0, 0).content == SlotContent::Midi);
    REQUIRE(session.slot(0, 0).midiClip.lengthTicks() == 960);

    command.undo();
    REQUIRE(session.slot(0, 0).content == SlotContent::Empty);
}

TEST_CASE("AddSessionMidiClipCommand no-ops when the slot is not empty", "[session]") {
    Session session;
    session.addTrackColumn();
    session.addScene();
    session.slot(0, 0).content = SlotContent::Audio;

    MidiClip clip;
    AddSessionMidiClipCommand command(session, 0, 0, clip);
    command.execute();

    REQUIRE(session.slot(0, 0).content == SlotContent::Audio);

    command.undo();
    REQUIRE(session.slot(0, 0).content == SlotContent::Audio);
}

TEST_CASE("AddSessionAudioClipCommand execute fills an empty slot, undo empties it again", "[session]") {
    Session session;
    session.addTrackColumn();
    session.addScene();

    AudioClip clip;
    clip.setSourcePath("test.wav");

    AddSessionAudioClipCommand command(session, 0, 0, clip);
    command.execute();

    REQUIRE(session.slot(0, 0).content == SlotContent::Audio);
    REQUIRE(session.slot(0, 0).audioClip.sourcePath() == "test.wav");

    command.undo();
    REQUIRE(session.slot(0, 0).content == SlotContent::Empty);
}

TEST_CASE("ClearSessionSlotCommand execute empties a slot, undo restores its content", "[session]") {
    Session session;
    session.addTrackColumn();
    session.addScene();

    MidiClip clip;
    clip.setLengthTicks(1920);
    session.slot(0, 0).content = SlotContent::Midi;
    session.slot(0, 0).midiClip = clip;

    ClearSessionSlotCommand command(session, 0, 0);
    command.execute();
    REQUIRE(session.slot(0, 0).content == SlotContent::Empty);

    command.undo();
    REQUIRE(session.slot(0, 0).content == SlotContent::Midi);
    REQUIRE(session.slot(0, 0).midiClip.lengthTicks() == 1920);
}
