// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: AddTrackCommand/RemoveTrackCommand and Mixer::prepare state preservation

#include "model/Arrangement.h"
#include "model/Commands.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using howl::model::AddTrackCommand;
using howl::model::Arrangement;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Mixer;
using howl::model::RemoveTrackCommand;
using howl::model::Send;
using howl::model::TrackKind;

TEST_CASE("AddTrackCommand execute grows the arrangement and mixer in lockstep, undo shrinks both", "[trackcommand]") {
    Arrangement arrangement;
    Mixer mixer;
    mixer.prepare(0, 44100.0, 512, 1);

    AddTrackCommand command(arrangement, mixer, "Track 1", TrackKind::Midi);

    REQUIRE(arrangement.numTracks() == 0);
    command.execute();
    REQUIRE(arrangement.numTracks() == 1);
    REQUIRE(arrangement.track(0).name == "Track 1");
    REQUIRE(arrangement.track(0).kind == TrackKind::Midi);

    command.undo();
    REQUIRE(arrangement.numTracks() == 0);
}

TEST_CASE("RemoveTrackCommand undo restores the removed track's name, kind, and clips at the same index", "[trackcommand]") {
    Arrangement arrangement;
    Mixer mixer;
    arrangement.addTrack("Keep Before", TrackKind::Midi);
    const std::size_t targetIndex = arrangement.addTrack("Doomed", TrackKind::Midi);
    arrangement.addTrack("Keep After", TrackKind::Midi);
    mixer.prepare(arrangement.numTracks(), 44100.0, 512, 1);

    MidiClipPlacement placement { 0, MidiClip() };
    arrangement.addMidiClipPlacement(targetIndex, placement);

    RemoveTrackCommand command(arrangement, mixer, targetIndex);
    command.execute();

    REQUIRE(arrangement.numTracks() == 2);
    REQUIRE(arrangement.track(targetIndex).name == "Keep After");

    command.undo();

    REQUIRE(arrangement.numTracks() == 3);
    REQUIRE(arrangement.track(targetIndex).name == "Doomed");
    REQUIRE(arrangement.track(targetIndex).kind == TrackKind::Midi);
    REQUIRE(arrangement.track(targetIndex).midiClips.size() == 1);
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
