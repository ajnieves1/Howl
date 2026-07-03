// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: ProjectSerializer save/load round-trip tests

#include "project/ProjectSerializer.h"

#include "dsp/BuiltInEffectFactory.h"
#include "model/Arrangement.h"
#include "model/AudioClip.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "model/Note.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using howl::dsp::BuiltInEffectFactory;
using howl::engine::EffectType;
using howl::model::Arrangement;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Mixer;
using howl::model::Note;
using howl::model::Send;
using howl::model::TrackKind;
using howl::project::ProjectSerializer;

TEST_CASE("ProjectSerializer round-trips tracks, clips, mixer state, effects, sends, and tempo", "[projectserializer]") {
    Arrangement arrangement;
    Mixer mixer;
    BuiltInEffectFactory factory;

    const std::size_t midiTrack = arrangement.addTrack("Lead", TrackKind::Midi);
    MidiClip clip;
    clip.setLengthTicks(1920);
    clip.addNote(Note { 60, 0.8f, 0, 480 });
    clip.addNote(Note { 64, 0.9f, 480, 480 });
    arrangement.addMidiClipPlacement(midiTrack, MidiClipPlacement { 0, clip });

    const std::size_t audioTrack = arrangement.addTrack("Audio 1", TrackKind::Audio);
    AudioClip audioClip;
    audioClip.setSourcePath("/fake/path.wav");
    arrangement.addAudioClipPlacement(audioTrack, AudioClipPlacement { 960, audioClip });

    mixer.prepare(arrangement.numTracks(), 44100.0, 512, 2);
    const std::size_t bus = mixer.addBus("Bus A");

    mixer.trackStrip(midiTrack).setGainDb(-6.0f);
    mixer.trackStrip(midiTrack).setPan(0.3f);
    mixer.trackStrip(midiTrack).setMuted(true);
    mixer.setTrackOutput(midiTrack, bus);
    mixer.addSend(midiTrack, Send { bus, 0.6f, false });

    auto gainEffect = factory.create(EffectType::Gain);
    gainEffect->prepare(44100.0, 512);
    gainEffect->setParameter(0, 0.25f);
    mixer.trackStrip(midiTrack).effects().add(std::move(gainEffect));

    auto eqEffect = factory.create(EffectType::Equalizer);
    eqEffect->prepare(44100.0, 512);
    const float originalEqLowGain = eqEffect->getParameter(1);
    mixer.busStrip(bus).effects().add(std::move(eqEffect));

    mixer.masterStrip().setGainDb(-3.0f);

    const double tempo = 98.5;
    const juce::var instruments; // not this test's concern

    const juce::String json = ProjectSerializer::save(arrangement, mixer, instruments, tempo);
    REQUIRE(json.contains("\"version\""));

    juce::var parsed;
    REQUIRE(juce::JSON::parse(json, parsed).wasOk());
    REQUIRE(static_cast<int>(parsed.getProperty("version", 0)) == 1);

    Arrangement loadedArrangement;
    Mixer loadedMixer;
    juce::var loadedInstruments;
    double loadedTempo = 0.0;

    const bool ok = ProjectSerializer::load(json, loadedArrangement, loadedMixer, factory, nullptr,
        loadedInstruments, loadedTempo);
    REQUIRE(ok);
    REQUIRE(loadedTempo == Catch::Approx(98.5));

    REQUIRE(loadedArrangement.numTracks() == 2);
    REQUIRE(loadedArrangement.track(midiTrack).name == "Lead");
    REQUIRE(loadedArrangement.track(midiTrack).kind == TrackKind::Midi);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips.size() == 1);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.lengthTicks() == 1920);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.notes().size() == 2);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.notes()[0].key == 60);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.notes()[1].startTick == 480);

    REQUIRE(loadedArrangement.track(audioTrack).kind == TrackKind::Audio);
    REQUIRE(loadedArrangement.track(audioTrack).audioClips.size() == 1);
    REQUIRE(loadedArrangement.track(audioTrack).audioClips[0].startTick == 960);
    REQUIRE(loadedArrangement.track(audioTrack).audioClips[0].clip.sourcePath() == "/fake/path.wav");

    REQUIRE(loadedMixer.trackStrip(midiTrack).gainDb() == Catch::Approx(-6.0f));
    REQUIRE(loadedMixer.trackStrip(midiTrack).pan() == Catch::Approx(0.3f));
    REQUIRE(loadedMixer.trackStrip(midiTrack).muted());
    REQUIRE(loadedMixer.numBuses() == 1);
    REQUIRE(loadedMixer.busName(0) == "Bus A");
    REQUIRE(loadedMixer.trackOutput(midiTrack) == 0);

    REQUIRE(loadedMixer.sends(midiTrack).size() == 1);
    REQUIRE(loadedMixer.sends(midiTrack)[0].busIndex == 0);
    REQUIRE(loadedMixer.sends(midiTrack)[0].level == Catch::Approx(0.6f).margin(1e-6));
    REQUIRE_FALSE(loadedMixer.sends(midiTrack)[0].preFader);

    REQUIRE(loadedMixer.trackStrip(midiTrack).effects().size() == 1);
    REQUIRE(std::string(loadedMixer.trackStrip(midiTrack).effects().at(0).displayName()) == "Gain");
    REQUIRE(loadedMixer.trackStrip(midiTrack).effects().at(0).getParameter(0) == Catch::Approx(0.25f).margin(1e-6));

    REQUIRE(loadedMixer.busStrip(0).effects().size() == 1);
    REQUIRE(std::string(loadedMixer.busStrip(0).effects().at(0).displayName()) == "EQ");
    REQUIRE(loadedMixer.busStrip(0).effects().at(0).getParameter(1) == Catch::Approx(originalEqLowGain).margin(1e-6));

    REQUIRE(loadedMixer.masterStrip().gainDb() == Catch::Approx(-3.0f));
}

TEST_CASE("ProjectSerializer.load is forward-tolerant of an unrecognized version and unknown keys", "[projectserializer]") {
    const juce::String json = R"({
        "version": 2,
        "somethingFromTheFuture": true,
        "tempo": 140.0,
        "tracks": [
            { "name": "Lead", "kind": "midi", "midiClips": [], "audioClips": [],
              "strip": { "gainDb": -1.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] },
              "output": -1, "sends": [], "extraKey": "ignored" }
        ],
        "buses": [],
        "masterStrip": { "gainDb": 0.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] }
    })";

    BuiltInEffectFactory factory;
    Arrangement arrangement;
    Mixer mixer;
    juce::var instruments;
    double tempo = 0.0;

    const bool ok = ProjectSerializer::load(json, arrangement, mixer, factory, nullptr, instruments, tempo);
    REQUIRE(ok);
    REQUIRE(tempo == Catch::Approx(140.0));
    REQUIRE(arrangement.numTracks() == 1);
    REQUIRE(arrangement.track(0).name == "Lead");
    REQUIRE(mixer.trackStrip(0).gainDb() == Catch::Approx(-1.0f));
}
