// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: session launch playback, bar-quantized switching through ArrangementNode

#include "dsp/SubtractiveSynth.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"
#include "model/MidiClip.h"
#include "model/Note.h"
#include "model/Session.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::dsp::SubtractiveSynth;
using howl::engine::Transport;
using howl::model::Arrangement;
using howl::model::ArrangementNode;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Note;
using howl::model::Session;
using howl::model::SlotContent;
using howl::model::TrackKind;

namespace {

// True if every sample in [start, end) is near-zero
bool isSilentRange(const std::vector<float>& buffer, int start, int end) {
    for (int i = start; i < end; ++i) {
        if (std::abs(buffer[static_cast<std::size_t>(i)]) > 1e-6f) {
            return false;
        }
    }
    return true;
}

// True if some sample in [start, end) is clearly non-silent
bool hasNonSilentSample(const std::vector<float>& buffer, int start, int end) {
    for (int i = start; i < end; ++i) {
        if (std::abs(buffer[static_cast<std::size_t>(i)]) > 1e-4f) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("ArrangementNode launches a session slot exactly at the next bar boundary", "[session]") {
    const double sampleRate = 44100.0;
    const int maxBlockSize = 200000;
    // 120 bpm at 44100 Hz: one bar (3840 ticks) is exactly 88200 samples
    constexpr int kBarSamples = 88200;

    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    Session session;
    session.addTrackColumn();
    session.addScene();

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4); // one bar
    clip.addNote(Note { 69, 1.0f, 0, kTicksPerQuarter * 2 }); // sounds for the first half of the bar
    session.slot(trackIndex, 0).content = SlotContent::Midi;
    session.slot(trackIndex, 0).midiClip = clip;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    SubtractiveSynth synth;
    synth.prepare(sampleRate, maxBlockSize);

    ArrangementNode node(transport, arrangement);
    node.setSession(&session);
    node.prepare(sampleRate, maxBlockSize, 1);
    node.setInstrumentForTrack(trackIndex, &synth);

    std::vector<float> buffer1(20000, 0.0f);
    float* channels1[1] = { buffer1.data() };
    AudioBlock block1 { channels1, 1, 20000 };
    const SampleCount pos1 = transport.advance(20000);
    REQUIRE(pos1 == 0);
    node.process(block1, pos1);

    REQUIRE(isSilentRange(buffer1, 0, 20000));

    REQUIRE(node.requestLaunch(trackIndex, 0));

    std::vector<float> buffer2(100000, 0.0f);
    float* channels2[1] = { buffer2.data() };
    AudioBlock block2 { channels2, 1, 100000 };
    const SampleCount pos2 = transport.advance(100000);
    REQUIRE(pos2 == 20000);
    node.process(block2, pos2);

    const int boundaryOffset = kBarSamples - 20000; // 68200, local index within block2

    REQUIRE(isSilentRange(buffer2, 0, boundaryOffset));
    REQUIRE(hasNonSilentSample(buffer2, boundaryOffset, boundaryOffset + 2000));
    REQUIRE(node.activeScene(trackIndex) == 0);
}

TEST_CASE("ArrangementNode loops a launched session slot, the onset repeats one loop later", "[session]") {
    const double sampleRate = 44100.0;
    const int maxBlockSize = 200000;
    constexpr int kBarSamples = 88200;

    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    Session session;
    session.addTrackColumn();
    session.addScene();

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4); // one bar
    clip.addNote(Note { 69, 1.0f, 0, kTicksPerQuarter * 2 }); // sounds for the first half of the bar
    session.slot(trackIndex, 0).content = SlotContent::Midi;
    session.slot(trackIndex, 0).midiClip = clip;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    SubtractiveSynth synth;
    synth.prepare(sampleRate, maxBlockSize);

    ArrangementNode node(transport, arrangement);
    node.setSession(&session);
    node.prepare(sampleRate, maxBlockSize, 1);
    node.setInstrumentForTrack(trackIndex, &synth);

    std::vector<float> buffer1(20000, 0.0f);
    float* channels1[1] = { buffer1.data() };
    AudioBlock block1 { channels1, 1, 20000 };
    node.process(block1, transport.advance(20000));

    REQUIRE(node.requestLaunch(trackIndex, 0));

    std::vector<float> buffer2(100000, 0.0f);
    float* channels2[1] = { buffer2.data() };
    AudioBlock block2 { channels2, 1, 100000 };
    node.process(block2, transport.advance(100000)); // pos 20000, first onset at local 68200

    std::vector<float> buffer3(100000, 0.0f);
    float* channels3[1] = { buffer3.data() };
    AudioBlock block3 { channels3, 1, 100000 };
    node.process(block3, transport.advance(100000)); // pos 120000

    // Loop restarts at absolute sample 88200 + 88200 = 176400, local index 56400 in block3
    const int secondOnsetOffset = 176400 - 120000;

    // Silence gap between the first onset's release tail and the second onset
    REQUIRE(isSilentRange(buffer3, 40000, secondOnsetOffset));
    // The onset repeats
    REQUIRE(hasNonSilentSample(buffer3, secondOnsetOffset, secondOnsetOffset + 2000));
}

TEST_CASE("ArrangementNode stops a session slot at the next bar boundary and the arrangement resumes", "[session]") {
    const double sampleRate = 44100.0;
    const int maxBlockSize = 200000;
    constexpr int kBarSamples = 88200;

    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    // A long, always-on arrangement note so resumption after the stop is audible
    MidiClip arrangementClip;
    arrangementClip.setLengthTicks(kTicksPerQuarter * 4000);
    arrangementClip.addNote(Note { 60, 1.0f, 0, kTicksPerQuarter * 4000 });
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, arrangementClip });

    Session session;
    session.addTrackColumn();
    session.addScene();

    MidiClip sessionClip;
    sessionClip.setLengthTicks(kTicksPerQuarter * 4); // one bar
    sessionClip.addNote(Note { 69, 1.0f, 0, kTicksPerQuarter * 2 }); // first half of the bar
    session.slot(trackIndex, 0).content = SlotContent::Midi;
    session.slot(trackIndex, 0).midiClip = sessionClip;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    SubtractiveSynth synth;
    synth.prepare(sampleRate, maxBlockSize);

    ArrangementNode node(transport, arrangement);
    node.setSession(&session);
    node.prepare(sampleRate, maxBlockSize, 1);
    node.setInstrumentForTrack(trackIndex, &synth);

    std::vector<float> buffer1(20000, 0.0f);
    float* channels1[1] = { buffer1.data() };
    AudioBlock block1 { channels1, 1, 20000 };
    node.process(block1, transport.advance(20000)); // pos 0, arrangement note sounds throughout

    REQUIRE(node.requestLaunch(trackIndex, 0));

    std::vector<float> buffer2(100000, 0.0f);
    float* channels2[1] = { buffer2.data() };
    AudioBlock block2 { channels2, 1, 100000 };
    node.process(block2, transport.advance(100000)); // pos 20000, launch activates at local 68200

    REQUIRE(node.activeScene(trackIndex) == 0);
    REQUIRE(node.requestStop(trackIndex));

    std::vector<float> buffer3(100000, 0.0f);
    float* channels3[1] = { buffer3.data() };
    AudioBlock block3 { channels3, 1, 100000 };
    node.process(block3, transport.advance(100000)); // pos 120000, stop activates at local 56400

    const int stopOffset = 176400 - 120000; // 56400

    // Pre-boundary: still the session's note, briefly, before its own release completes
    REQUIRE(hasNonSilentSample(buffer3, 0, 5000));
    // Post-boundary: the stop is honored, silence for the rest of this block
    REQUIRE(isSilentRange(buffer3, stopOffset + 500, 100000));
    REQUIRE(node.activeScene(trackIndex) == -1);

    std::vector<float> buffer4(50000, 0.0f);
    float* channels4[1] = { buffer4.data() };
    AudioBlock block4 { channels4, 1, 50000 };
    node.process(block4, transport.advance(50000)); // pos 220000, arrangement resumes

    REQUIRE(hasNonSilentSample(buffer4, 0, 50000));
}

TEST_CASE("ArrangementNode flushes held notes on a scene switch, no stuck note", "[session]") {
    const double sampleRate = 44100.0;
    const int maxBlockSize = 200000;

    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    Session session;
    session.addTrackColumn();
    session.addScene();
    session.addScene();

    // Scene 0: a note spanning the entire bar, still held when we switch away from it
    MidiClip heldClip;
    heldClip.setLengthTicks(kTicksPerQuarter * 4);
    heldClip.addNote(Note { 69, 1.0f, 0, kTicksPerQuarter * 4 });
    session.slot(trackIndex, 0).content = SlotContent::Midi;
    session.slot(trackIndex, 0).midiClip = heldClip;

    // Scene 1: empty, nothing to trigger a note-off on its own
    MidiClip emptyClip;
    emptyClip.setLengthTicks(kTicksPerQuarter * 4);
    session.slot(trackIndex, 1).content = SlotContent::Midi;
    session.slot(trackIndex, 1).midiClip = emptyClip;

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    SubtractiveSynth synth;
    synth.prepare(sampleRate, maxBlockSize);

    ArrangementNode node(transport, arrangement);
    node.setSession(&session);
    node.prepare(sampleRate, maxBlockSize, 1);
    node.setInstrumentForTrack(trackIndex, &synth);

    std::vector<float> buffer1(20000, 0.0f);
    float* channels1[1] = { buffer1.data() };
    AudioBlock block1 { channels1, 1, 20000 };
    node.process(block1, transport.advance(20000)); // pos 0

    REQUIRE(node.requestLaunch(trackIndex, 0));

    std::vector<float> buffer2(100000, 0.0f);
    float* channels2[1] = { buffer2.data() };
    AudioBlock block2 { channels2, 1, 100000 };
    node.process(block2, transport.advance(100000)); // pos 20000, scene 0 activates at local 68200

    REQUIRE(node.activeScene(trackIndex) == 0);
    REQUIRE(node.requestLaunch(trackIndex, 1)); // switch away while the note is still held

    std::vector<float> buffer3(100000, 0.0f);
    float* channels3[1] = { buffer3.data() };
    AudioBlock block3 { channels3, 1, 100000 };
    node.process(block3, transport.advance(100000)); // pos 120000, switch to scene 1 at local 56400

    REQUIRE(node.activeScene(trackIndex) == 1);

    // Well past the synth's 0.2s release tail: the flushed note must have fully decayed
    REQUIRE(isSilentRange(buffer3, 90000, 100000));
}
