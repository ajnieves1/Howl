// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: ProjectSerializer save/load round-trip tests

#include "project/ProjectSerializer.h"

#include "dsp/BuiltInEffectFactory.h"
#include "model/Arrangement.h"
#include "model/AudioClip.h"
#include "model/AutomationLane.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "model/Note.h"
#include "model/Pattern.h"
#include "model/Session.h"
#include "plugins/IPluginInstance.h"
#include "plugins/PluginDescriptor.h"
#include "plugins/PluginEffect.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using howl::dsp::BuiltInEffectFactory;
using howl::engine::EffectType;
using howl::model::Arrangement;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::AutomationLaneSlot;
using howl::model::AutomationPoint;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Mixer;
using howl::model::Note;
using howl::model::PatternBank;
using howl::model::PatternPlacement;
using howl::model::Send;
using howl::model::Session;
using howl::model::SlotContent;
using howl::model::TrackKind;
using howl::plugins::IPluginHost;
using howl::plugins::IPluginInstance;
using howl::plugins::ParamInfo;
using howl::plugins::PluginDescriptor;
using howl::plugins::PluginEffect;
using howl::plugins::StateBlob;
using howl::project::ProjectSerializer;

namespace {

// Returns whatever state it is given by loadState(), returns the fixed seed on saveState()
class StatefulStubPluginInstance : public IPluginInstance {
public:
    StateBlob stateToReturn;
    StateBlob lastLoadedState;

    void prepare(double, int) override {
    }
    void release() override {
    }
    void process(howl::AudioBlock&, const void*) override {
    }
    StateBlob saveState() const override {
        return stateToReturn;
    }
    void loadState(const StateBlob& state) override {
        lastLoadedState = state;
    }
    const std::vector<ParamInfo>& params() const override {
        return m_params;
    }
    void setParamNormalized(uint32_t, float) override {
    }
    bool hasEditor() const override {
        return false;
    }
    juce::Component* openEditor() override {
        return nullptr;
    }
    void closeEditor() override {
    }

private:
    std::vector<ParamInfo> m_params;
};

// Minimal host: instantiate() always hands back a fresh StatefulStubPluginInstance,
// list() advertises one descriptor matching what the test's saved effect used
class StatefulStubPluginHost : public IPluginHost {
public:
    StateBlob seedState;
    StatefulStubPluginInstance* lastInstantiated = nullptr;

    void rescan() override {
    }
    std::vector<PluginDescriptor> list() const override {
        return { PluginDescriptor { "TestSynth", "TestVendor", "VST3", "/fake/TestSynth.vst3", true } };
    }
    std::unique_ptr<IPluginInstance> instantiate(const PluginDescriptor&) override {
        auto instance = std::make_unique<StatefulStubPluginInstance>();
        instance->stateToReturn = seedState;
        lastInstantiated = instance.get();
        return instance;
    }
};

} // namespace

TEST_CASE("ProjectSerializer round-trips tracks, clips, mixer state, effects, sends, and tempo", "[projectserializer]") {
    Arrangement arrangement;
    Mixer mixer;
    BuiltInEffectFactory factory;

    const std::size_t midiTrack = arrangement.addTrack("Lead", TrackKind::Midi);
    arrangement.track(midiTrack).color = 0xFF00FF00; // a non default green to prove color persists
    MidiClip clip;
    clip.setLengthTicks(1920);
    clip.addNote(Note { 60, 0.8f, 0, 480 });
    clip.addNote(Note { 64, 0.9f, 480, 480 });
    arrangement.addMidiClipPlacement(midiTrack, MidiClipPlacement { 0, clip, true });

    const std::size_t audioTrack = arrangement.addTrack("Audio 1", TrackKind::Audio);
    AudioClip audioClip;
    audioClip.setSourcePath("/fake/path.wav");
    audioClip.setOriginalBpm(140.0);
    audioClip.setWarpEnabled(true);
    arrangement.addAudioClipPlacement(audioTrack, AudioClipPlacement { 960, audioClip });

    Session session;
    session.addScene();
    session.addScene();
    session.addTrackColumn(); // midiTrack's column
    session.addTrackColumn(); // audioTrack's column

    MidiClip sessionMidiClip;
    sessionMidiClip.setLengthTicks(1920);
    sessionMidiClip.addNote(Note { 62, 0.7f, 0, 480 });
    sessionMidiClip.addNote(Note { 65, 0.6f, 480, 480 });
    session.slot(midiTrack, 0).content = SlotContent::Midi;
    session.slot(midiTrack, 0).midiClip = sessionMidiClip;

    AudioClip sessionAudioClip;
    sessionAudioClip.setSourcePath("/fake/session.wav");
    sessionAudioClip.setOriginalBpm(98.0);
    sessionAudioClip.setWarpEnabled(true);
    session.slot(audioTrack, 1).content = SlotContent::Audio;
    session.slot(audioTrack, 1).audioClip = sessionAudioClip;

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

    AutomationLaneSlot laneSlot;
    laneSlot.paramIndex = 2;
    laneSlot.lane.addPoint(AutomationPoint { 0, 0.1f });
    laneSlot.lane.addPoint(AutomationPoint { 480, 0.5f });
    laneSlot.lane.addPoint(AutomationPoint { 960, 0.9f });
    arrangement.track(midiTrack).automation.push_back(laneSlot);

    PatternBank patterns; // not this test's concern, covered by its own round trip test

    const double tempo = 98.5;
    const juce::var instruments; // not this test's concern

    juce::Array<juce::var> mappingsArray;
    auto* mappingObj = new juce::DynamicObject();
    mappingObj->setProperty("cc", 74);
    mappingObj->setProperty("stripKind", "track");
    mappingObj->setProperty("stripIndex", static_cast<int>(midiTrack));
    mappingObj->setProperty("effectIndex", 0);
    mappingObj->setProperty("paramIndex", 0);
    mappingsArray.add(juce::var(mappingObj));
    const juce::var midiMappings(mappingsArray);

    const juce::String json = ProjectSerializer::save(arrangement, mixer, session, patterns, instruments, tempo,
        midiMappings);
    REQUIRE(json.contains("\"version\""));

    juce::var parsed;
    REQUIRE(juce::JSON::parse(json, parsed).wasOk());
    REQUIRE(static_cast<int>(parsed.getProperty("version", 0)) == 1);

    Arrangement loadedArrangement;
    Mixer loadedMixer;
    Session loadedSession;
    PatternBank loadedPatterns;
    juce::var loadedInstruments;
    double loadedTempo = 0.0;
    juce::var loadedMidiMappings;

    const bool ok = ProjectSerializer::load(json, loadedArrangement, loadedMixer, loadedSession, loadedPatterns,
        factory, nullptr, loadedInstruments, loadedTempo, loadedMidiMappings);
    REQUIRE(ok);
    REQUIRE(loadedTempo == Catch::Approx(98.5));

    REQUIRE(loadedArrangement.track(midiTrack).automation.size() == 1);
    REQUIRE(loadedArrangement.track(midiTrack).automation[0].paramIndex == 2);
    REQUIRE(loadedArrangement.track(midiTrack).automation[0].lane.points().size() == 3);
    REQUIRE(loadedArrangement.track(midiTrack).automation[0].lane.points()[1].tick == 480);
    REQUIRE(loadedArrangement.track(midiTrack).automation[0].lane.points()[1].value == Catch::Approx(0.5f));

    const auto* loadedMappingsArray = loadedMidiMappings.getArray();
    REQUIRE(loadedMappingsArray != nullptr);
    REQUIRE(loadedMappingsArray->size() == 1);
    REQUIRE(static_cast<int>((*loadedMappingsArray)[0].getProperty("cc", -1)) == 74);
    REQUIRE((*loadedMappingsArray)[0].getProperty("stripKind", juce::var()).toString() == "track");

    REQUIRE(loadedArrangement.numTracks() == 2);
    REQUIRE(loadedArrangement.track(midiTrack).name == "Lead");
    REQUIRE(loadedArrangement.track(midiTrack).kind == TrackKind::Midi);
    REQUIRE(loadedArrangement.track(midiTrack).color == 0xFF00FF00u);
    REQUIRE(loadedArrangement.track(audioTrack).color == howl::model::kDefaultChannelColor);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips.size() == 1);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.lengthTicks() == 1920);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.notes().size() == 2);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.notes()[0].key == 60);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].clip.notes()[1].startTick == 480);
    REQUIRE(loadedArrangement.track(midiTrack).midiClips[0].muted);

    REQUIRE(loadedArrangement.track(audioTrack).kind == TrackKind::Audio);
    REQUIRE(loadedArrangement.track(audioTrack).audioClips.size() == 1);
    REQUIRE(loadedArrangement.track(audioTrack).audioClips[0].startTick == 960);
    REQUIRE(loadedArrangement.track(audioTrack).audioClips[0].clip.sourcePath() == "/fake/path.wav");
    REQUIRE(loadedArrangement.track(audioTrack).audioClips[0].clip.originalBpm() == Catch::Approx(140.0));
    REQUIRE(loadedArrangement.track(audioTrack).audioClips[0].clip.warpEnabled());
    REQUIRE_FALSE(loadedArrangement.track(audioTrack).audioClips[0].muted);

    REQUIRE(loadedSession.numTracks() == 2);
    REQUIRE(loadedSession.numScenes() == 2);
    REQUIRE(loadedSession.slot(midiTrack, 0).content == SlotContent::Midi);
    REQUIRE(loadedSession.slot(midiTrack, 0).midiClip.lengthTicks() == 1920);
    REQUIRE(loadedSession.slot(midiTrack, 0).midiClip.notes().size() == 2);
    REQUIRE(loadedSession.slot(midiTrack, 0).midiClip.notes()[0].key == 62);
    REQUIRE(loadedSession.slot(midiTrack, 1).content == SlotContent::Empty);
    REQUIRE(loadedSession.slot(audioTrack, 1).content == SlotContent::Audio);
    REQUIRE(loadedSession.slot(audioTrack, 1).audioClip.sourcePath() == "/fake/session.wav");
    REQUIRE(loadedSession.slot(audioTrack, 1).audioClip.originalBpm() == Catch::Approx(98.0));
    REQUIRE(loadedSession.slot(audioTrack, 1).audioClip.warpEnabled());

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
    Session session;
    PatternBank patterns;
    juce::var instruments;
    double tempo = 0.0;
    juce::var midiMappings;

    const bool ok = ProjectSerializer::load(json, arrangement, mixer, session, patterns, factory, nullptr,
        instruments, tempo, midiMappings);
    REQUIRE(ok);
    REQUIRE(tempo == Catch::Approx(140.0));
    REQUIRE(arrangement.numTracks() == 1);
    REQUIRE(arrangement.track(0).name == "Lead");
    REQUIRE(mixer.trackStrip(0).gainDb() == Catch::Approx(-1.0f));

    // No "session" key in this version 2 file, so session is empty but sized to the track count
    REQUIRE(session.numTracks() == 1);
    REQUIRE(session.numScenes() == 0);

    // No "automation" or "midiMappings" keys in this version 2 file: empty, not a crash
    REQUIRE(arrangement.track(0).automation.empty());
    REQUIRE(midiMappings.isVoid());

    // No "patterns"/"patternPlacements" keys either: an empty bank, not a crash
    REQUIRE(patterns.numPatterns() == 0);
    REQUIRE(patterns.placements().empty());
}

TEST_CASE("ProjectSerializer defaults a placement's muted flag to false when the key is absent", "[projectserializer]") {
    const juce::String json = R"({
        "version": 1,
        "tempo": 120.0,
        "tracks": [
            { "name": "Lead", "kind": "midi",
              "midiClips": [ { "startTick": 0, "lengthTicks": 960, "notes": [] } ],
              "audioClips": [],
              "strip": { "gainDb": 0.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] },
              "output": -1, "sends": [] },
            { "name": "Audio 1", "kind": "audio",
              "midiClips": [],
              "audioClips": [ { "startTick": 0, "sourcePath": "/fake/path.wav", "originalBpm": 120.0, "warpEnabled": false } ],
              "strip": { "gainDb": 0.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] },
              "output": -1, "sends": [] }
        ],
        "buses": [],
        "masterStrip": { "gainDb": 0.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] }
    })";

    BuiltInEffectFactory factory;
    Arrangement arrangement;
    Mixer mixer;
    Session session;
    PatternBank patterns;
    juce::var instruments;
    double tempo = 0.0;
    juce::var midiMappings;

    const bool ok = ProjectSerializer::load(json, arrangement, mixer, session, patterns, factory, nullptr,
        instruments, tempo, midiMappings);
    REQUIRE(ok);
    REQUIRE_FALSE(arrangement.track(0).midiClips[0].muted);
    REQUIRE_FALSE(arrangement.track(1).audioClips[0].muted);
}

TEST_CASE("ProjectSerializer preserves a plugin effect's state bytes exactly through save and load", "[projectserializer]") {
    Arrangement arrangement;
    Mixer mixer;
    BuiltInEffectFactory factory;

    const std::size_t track = arrangement.addTrack("Lead", TrackKind::Midi);
    mixer.prepare(arrangement.numTracks(), 44100.0, 512, 2);

    // Includes a zero byte and a 0xFF byte on purpose, exactly the kind of bytes a Base64
    // round trip bug would corrupt or drop
    const StateBlob seedBytes { 0x00, 0x01, 0xFF, 0x10, 0x00, 0x2A, 0x7F };

    auto savingInstance = std::make_unique<StatefulStubPluginInstance>();
    savingInstance->stateToReturn = seedBytes;

    const PluginDescriptor descriptor { "TestSynth", "TestVendor", "VST3", "/fake/TestSynth.vst3", true };
    auto effect = std::make_unique<PluginEffect>(std::move(savingInstance), descriptor);
    mixer.trackStrip(track).effects().add(std::move(effect));

    const juce::var instruments;
    const juce::var midiMappings;
    PatternBank patterns;
    const juce::String json = ProjectSerializer::save(arrangement, mixer, Session(), patterns, instruments, 120.0,
        midiMappings);

    StatefulStubPluginHost host;

    Arrangement loadedArrangement;
    Mixer loadedMixer;
    Session loadedSession;
    PatternBank loadedPatterns;
    juce::var loadedInstruments;
    double loadedTempo = 0.0;
    juce::var loadedMidiMappings;

    const bool ok = ProjectSerializer::load(json, loadedArrangement, loadedMixer, loadedSession, loadedPatterns,
        factory, &host, loadedInstruments, loadedTempo, loadedMidiMappings);
    REQUIRE(ok);

    REQUIRE(loadedMixer.trackStrip(track).effects().size() == 1);
    REQUIRE(host.lastInstantiated != nullptr);
    REQUIRE(host.lastInstantiated->lastLoadedState == seedBytes);
}

TEST_CASE("ProjectSerializer round trips a pattern bank's notes and placements", "[projectserializer]") {
    Arrangement arrangement;
    Mixer mixer;
    BuiltInEffectFactory factory;

    arrangement.addTrack("Lead", TrackKind::Midi);
    arrangement.addTrack("Bass", TrackKind::Midi);
    mixer.prepare(arrangement.numTracks(), 44100.0, 512, 2);

    PatternBank patterns;
    const std::size_t patternA = patterns.addPattern("Verse", 2);
    patterns.pattern(patternA).trackClips[0].setLengthTicks(1920);
    patterns.pattern(patternA).trackClips[0].addNote(Note { 60, 0.8f, 0, 480 });
    patterns.pattern(patternA).trackClips[1].setLengthTicks(960);
    patterns.pattern(patternA).trackClips[1].addNote(Note { 36, 1.0f, 0, 240 });

    const std::size_t patternB = patterns.addPattern("Chorus", 2);
    patterns.pattern(patternB).trackClips[0].setLengthTicks(3840);
    patterns.pattern(patternB).trackClips[0].addNote(Note { 67, 0.9f, 0, 960 });
    // patternB's second lane is deliberately left untouched, the default/null lane path

    patterns.addPlacement(PatternPlacement { patternA, 0, 0, false, 0 });
    patterns.addPlacement(PatternPlacement { patternB, 3840, 1, true, 0 });

    const juce::var instruments;
    const juce::var midiMappings;
    const juce::String json = ProjectSerializer::save(arrangement, mixer, Session(), patterns, instruments, 120.0,
        midiMappings);

    Arrangement loadedArrangement;
    Mixer loadedMixer;
    Session loadedSession;
    PatternBank loadedPatterns;
    juce::var loadedInstruments;
    double loadedTempo = 0.0;
    juce::var loadedMidiMappings;

    const bool ok = ProjectSerializer::load(json, loadedArrangement, loadedMixer, loadedSession, loadedPatterns,
        factory, nullptr, loadedInstruments, loadedTempo, loadedMidiMappings);
    REQUIRE(ok);

    REQUIRE(loadedPatterns.numPatterns() == 2);
    REQUIRE(loadedPatterns.pattern(patternA).name == "Verse");
    REQUIRE(loadedPatterns.pattern(patternA).trackClips.size() == 2);
    REQUIRE(loadedPatterns.pattern(patternA).trackClips[0].lengthTicks() == 1920);
    REQUIRE(loadedPatterns.pattern(patternA).trackClips[0].notes().size() == 1);
    REQUIRE(loadedPatterns.pattern(patternA).trackClips[0].notes()[0].key == 60);
    REQUIRE(loadedPatterns.pattern(patternA).trackClips[1].lengthTicks() == 960);
    REQUIRE(loadedPatterns.pattern(patternA).trackClips[1].notes()[0].key == 36);

    REQUIRE(loadedPatterns.pattern(patternB).name == "Chorus");
    REQUIRE(loadedPatterns.pattern(patternB).trackClips[0].notes()[0].key == 67);
    REQUIRE(loadedPatterns.pattern(patternB).trackClips[1].lengthTicks() == 0);
    REQUIRE(loadedPatterns.pattern(patternB).trackClips[1].notes().empty());

    REQUIRE(loadedPatterns.placements().size() == 2);
    REQUIRE(loadedPatterns.placements()[0].patternIndex == patternA);
    REQUIRE(loadedPatterns.placements()[0].startTick == 0);
    REQUIRE(loadedPatterns.placements()[0].laneIndex == 0);
    REQUIRE_FALSE(loadedPatterns.placements()[0].muted);
    REQUIRE(loadedPatterns.placements()[1].patternIndex == patternB);
    REQUIRE(loadedPatterns.placements()[1].startTick == 3840);
    REQUIRE(loadedPatterns.placements()[1].laneIndex == 1);
    REQUIRE(loadedPatterns.placements()[1].muted);
}

TEST_CASE("A pattern placement saved before lanes existed loads onto the first lane, unmuted", "[project]") {
    BuiltInEffectFactory factory;

    const juce::String json = R"({
        "version": 1,
        "tempo": 120.0,
        "tracks": [ { "name": "Lead", "kind": "midi", "midiClips": [], "audioClips": [] } ],
        "patterns": [ { "name": "Verse", "trackClips": [ { "lengthTicks": 1920, "notes": [] } ] } ],
        "patternPlacements": [ { "pattern": 0, "startTick": 960 } ]
    })";

    Arrangement arrangement;
    Mixer mixer;
    Session session;
    PatternBank patterns;
    juce::var instruments;
    double tempo = 0.0;
    juce::var midiMappings;

    const bool ok = ProjectSerializer::load(json, arrangement, mixer, session, patterns, factory, nullptr,
        instruments, tempo, midiMappings);
    REQUIRE(ok);

    REQUIRE(patterns.placements().size() == 1);
    REQUIRE(patterns.placements()[0].startTick == 960);
    REQUIRE(patterns.placements()[0].laneIndex == 0);
    REQUIRE_FALSE(patterns.placements()[0].muted);
    REQUIRE(patterns.placements()[0].id != 0);
}
