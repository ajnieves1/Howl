// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AddTrackCommand/RemoveTrackCommand and Mixer::prepare state preservation

#include "model/Arrangement.h"
#include "model/Commands.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "model/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using howl::model::AddTrackCommand;
using howl::model::Arrangement;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Mixer;
using howl::model::RemoveTrackCommand;
using howl::model::Send;
using howl::model::Session;
using howl::model::TrackKind;

TEST_CASE("AddTrackCommand execute grows the arrangement, mixer, and session in lockstep, undo shrinks all three", "[trackcommand]") {
    Arrangement arrangement;
    Mixer mixer;
    mixer.prepare(0, 44100.0, 512, 1);
    Session session;

    AddTrackCommand command(arrangement, mixer, session, "Track 1", TrackKind::Midi);

    REQUIRE(arrangement.numTracks() == 0);
    REQUIRE(session.numTracks() == 0);
    command.execute();
    REQUIRE(arrangement.numTracks() == 1);
    REQUIRE(arrangement.track(0).name == "Track 1");
    REQUIRE(arrangement.track(0).kind == TrackKind::Midi);
    REQUIRE(session.numTracks() == 1);

    command.undo();
    REQUIRE(arrangement.numTracks() == 0);
    REQUIRE(session.numTracks() == 0);
}

TEST_CASE("RemoveTrackCommand undo restores the removed track's name, kind, clips, and session column at the same index", "[trackcommand]") {
    Arrangement arrangement;
    Mixer mixer;
    Session session;
    arrangement.addTrack("Keep Before", TrackKind::Midi);
    const std::size_t targetIndex = arrangement.addTrack("Doomed", TrackKind::Midi);
    arrangement.addTrack("Keep After", TrackKind::Midi);
    mixer.prepare(arrangement.numTracks(), 44100.0, 512, 1);
    session.addTrackColumn();
    session.addTrackColumn();
    session.addTrackColumn();
    session.addScene();
    session.slot(targetIndex, 0).content = howl::model::SlotContent::Midi;

    MidiClipPlacement placement { 0, MidiClip() };
    arrangement.addMidiClipPlacement(targetIndex, placement);

    RemoveTrackCommand command(arrangement, mixer, session, targetIndex);
    command.execute();

    REQUIRE(arrangement.numTracks() == 2);
    REQUIRE(arrangement.track(targetIndex).name == "Keep After");
    REQUIRE(session.numTracks() == 2);

    command.undo();

    REQUIRE(arrangement.numTracks() == 3);
    REQUIRE(arrangement.track(targetIndex).name == "Doomed");
    REQUIRE(arrangement.track(targetIndex).kind == TrackKind::Midi);
    REQUIRE(arrangement.track(targetIndex).midiClips.size() == 1);
    REQUIRE(session.numTracks() == 3);
    REQUIRE(session.slot(targetIndex, 0).content == howl::model::SlotContent::Midi);
}

TEST_CASE("Mixer.prepare called twice preserves gain, routing, and sends", "[trackcommand]") {
    Mixer mixer;
    mixer.prepare(2, 44100.0, 512, 1);

    const std::size_t bus = mixer.addBus("Bus A");
    mixer.trackStrip(0).setGainDb(-6.0f);
    mixer.setTrackOutput(0, bus);
    mixer.addSend(0, Send { bus, 0.5f, false });

    mixer.prepare(2, 44100.0, 512, 1);

    REQUIRE(mixer.trackStrip(0).gainDb() == Catch::Approx(-6.0f));
    REQUIRE(mixer.trackOutput(0) == bus);
    REQUIRE(mixer.sends(0).size() == 1);
    REQUIRE(mixer.sends(0)[0].level == Catch::Approx(0.5f));
}
