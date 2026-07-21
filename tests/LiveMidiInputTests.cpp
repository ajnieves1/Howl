// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: ArrangementNode live MIDI note routing to the selected track's instrument

#include "core/MidiEvent.h"
#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"
#include "model/MidiClip.h"
#include "model/Note.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using howl::AudioBlock;
using howl::LockFreeQueue;
using howl::MidiEvent;
using howl::SampleCount;
using howl::engine::Instrument;
using howl::engine::Transport;
using howl::model::Arrangement;
using howl::model::ArrangementNode;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Note;
using howl::model::TrackKind;

namespace {

// Records every noteOn/noteOff call it receives, otherwise a silent no-op instrument
class ProbeInstrument : public Instrument {
public:
    void prepare(double, int) override {
    }

    void noteOn(int key, float velocity) noexcept override {
        events.push_back(Event { true, key, velocity });
    }

    void noteOff(int key) noexcept override {
        events.push_back(Event { false, key, 0.0f });
    }

    void render(AudioBlock& audio) noexcept override {
        for (int channel = 0; channel < audio.numChannels; ++channel) {
            for (int frame = 0; frame < audio.numFrames; ++frame) {
                audio.channels[channel][frame] = 0.0f;
            }
        }
    }

    int numParameters() const override {
        return 0;
    }

    const char* parameterName(int) const override {
        return "";
    }

    void setParameter(int, float) noexcept override {
    }

    float getParameter(int) const noexcept override {
        return 0.0f;
    }

    struct Event {
        bool isNoteOn;
        int key;
        float velocity;
    };

    std::vector<Event> events;
};

// Runs one silent block through the node, draining whatever live MIDI is queued
void processOneBlock(ArrangementNode& node, Transport& transport, int blockSize) {
    std::vector<float> bufferL(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> bufferR(static_cast<std::size_t>(blockSize), 0.0f);
    float* channels[2] = { bufferL.data(), bufferR.data() };

    const SampleCount pos = transport.advance(blockSize);
    AudioBlock block { channels, 2, blockSize };
    node.process(block, pos);
}

} // namespace

TEST_CASE("ArrangementNode routes a queued live note to the selected track's instrument", "[model][live-midi]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    Transport transport;
    ArrangementNode node(transport, arrangement);
    node.prepare(44100.0, 512, 2);

    ProbeInstrument probe;
    node.setInstrumentForTrack(trackIndex, &probe);

    LockFreeQueue<MidiEvent, 256> queue;
    node.setLiveNoteQueue(&queue);
    node.setLiveTargetTrack(static_cast<std::ptrdiff_t>(trackIndex));

    queue.push(MidiEvent { MidiEvent::Type::NoteOn, 60, 0.8f });
    queue.push(MidiEvent { MidiEvent::Type::NoteOff, 60, 0.0f });

    processOneBlock(node, transport, 512);

    REQUIRE(probe.events.size() == 2);
    REQUIRE(probe.events[0].isNoteOn);
    REQUIRE(probe.events[0].key == 60);
    REQUIRE(probe.events[0].velocity == 0.8f);
    REQUIRE_FALSE(probe.events[1].isNoteOn);
    REQUIRE(probe.events[1].key == 60);
}

TEST_CASE("ArrangementNode releases held sequenced notes when the transport stops", "[model][transport-stop]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    // A note starting at tick 0 that lasts far past the blocks we process, so its note off is
    // never reached before the stop
    MidiClip clip;
    clip.addNote(Note { 60, 0.8f, 0, kTicksPerQuarter * 64 });
    clip.setLengthTicks(kTicksPerQuarter * 64);
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, clip });

    Transport transport;
    ArrangementNode node(transport, arrangement);
    node.prepare(44100.0, 512, 2);

    ProbeInstrument probe;
    node.setInstrumentForTrack(trackIndex, &probe);

    transport.play();
    processOneBlock(node, transport, 512);

    REQUIRE_FALSE(probe.events.empty());
    REQUIRE(probe.events.front().isNoteOn);
    REQUIRE(probe.events.front().key == 60);

    const std::size_t eventsWhilePlaying = probe.events.size();

    transport.stop();
    processOneBlock(node, transport, 512);

    // The stop edge released the held note: a note off for key 60 arrived after playback halted
    bool releasedHeldKey = false;
    for (std::size_t i = eventsWhilePlaying; i < probe.events.size(); ++i) {
        if (!probe.events[i].isNoteOn && probe.events[i].key == 60) {
            releasedHeldKey = true;
        }
    }
    REQUIRE(releasedHeldKey);
}

TEST_CASE("ArrangementNode plays live notes while the transport is stopped", "[model][live-midi]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    Transport transport; // never played

    ArrangementNode node(transport, arrangement);
    node.prepare(44100.0, 512, 2);

    ProbeInstrument probe;
    node.setInstrumentForTrack(trackIndex, &probe);

    LockFreeQueue<MidiEvent, 256> queue;
    node.setLiveNoteQueue(&queue);
    node.setLiveTargetTrack(static_cast<std::ptrdiff_t>(trackIndex));

    queue.push(MidiEvent { MidiEvent::Type::NoteOn, 64, 0.5f });

    processOneBlock(node, transport, 512);

    REQUIRE(probe.events.size() == 1);
    REQUIRE(probe.events[0].isNoteOn);
}

TEST_CASE("ArrangementNode drops a live note whose target track is frozen", "[model][live-midi]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    Transport transport;
    ArrangementNode node(transport, arrangement);
    node.prepare(44100.0, 512, 2);

    ProbeInstrument probe;
    node.setInstrumentForTrack(trackIndex, &probe);
    node.setFrozen(trackIndex, std::vector<std::vector<float>> { { 0.0f, 0.0f }, { 0.0f, 0.0f } });

    LockFreeQueue<MidiEvent, 256> queue;
    node.setLiveNoteQueue(&queue);
    node.setLiveTargetTrack(static_cast<std::ptrdiff_t>(trackIndex));

    queue.push(MidiEvent { MidiEvent::Type::NoteOn, 60, 0.8f });

    processOneBlock(node, transport, 512);

    REQUIRE(probe.events.empty());
}

TEST_CASE("ArrangementNode drains live notes with no target selected, discarding them", "[model][live-midi]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    Transport transport;
    ArrangementNode node(transport, arrangement);
    node.prepare(44100.0, 512, 2);

    ProbeInstrument probe;
    node.setInstrumentForTrack(trackIndex, &probe);

    LockFreeQueue<MidiEvent, 256> queue;
    node.setLiveNoteQueue(&queue);
    // setLiveTargetTrack is never called here, it defaults to -1

    queue.push(MidiEvent { MidiEvent::Type::NoteOn, 60, 0.8f });

    processOneBlock(node, transport, 512);

    REQUIRE(probe.events.empty());

    // Selecting the track afterward must not replay the already-drained event
    node.setLiveTargetTrack(static_cast<std::ptrdiff_t>(trackIndex));
    processOneBlock(node, transport, 512);
    REQUIRE(probe.events.empty());
}
