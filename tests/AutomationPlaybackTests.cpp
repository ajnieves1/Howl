// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: automation lane block-rate playback in MidiTrackRenderer, and the five automation commands

#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/Commands.h"
#include "model/MidiTrackRenderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::engine::Instrument;
using howl::engine::Transport;
using howl::model::AddAutomationLaneCommand;
using howl::model::AddAutomationPointCommand;
using howl::model::Arrangement;
using howl::model::AutomationLane;
using howl::model::AutomationLaneSlot;
using howl::model::AutomationPoint;
using howl::model::MidiTrackRenderer;
using howl::model::MoveAutomationPointCommand;
using howl::model::RemoveAutomationLaneCommand;
using howl::model::RemoveAutomationPointCommand;
using howl::model::Track;
using howl::model::TrackKind;

namespace {

// Records every setParameter call it receives, otherwise a silent no-op instrument
class ProbeInstrument : public Instrument {
public:
    void prepare(double, int) override {
    }

    void noteOn(int, float) noexcept override {
    }

    void noteOff(int) noexcept override {
    }

    void render(AudioBlock& audio) noexcept override {
        for (int channel = 0; channel < audio.numChannels; ++channel) {
            for (int frame = 0; frame < audio.numFrames; ++frame) {
                audio.channels[channel][frame] = 0.0f;
            }
        }
    }

    int numParameters() const override {
        return 4;
    }

    const char* parameterName(int) const override {
        return "Param";
    }

    void setParameter(int index, float value) noexcept override {
        calls.push_back(Call { index, value });
    }

    float getParameter(int) const noexcept override {
        return 0.0f;
    }

    struct Call {
        int index;
        float value;
    };

    std::vector<Call> calls;
};

} // namespace

TEST_CASE("MidiTrackRenderer evaluates automation while playing, ramping from a two-point lane", "[automation]") {
    Track track;
    track.name = "Lead";
    track.kind = TrackKind::Midi;
    track.automation.push_back(AutomationLaneSlot { 0, AutomationLane() });
    track.automation[0].lane.addPoint(AutomationPoint { 0, 0.0f });
    track.automation[0].lane.addPoint(AutomationPoint { 960, 1.0f });

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument probe;
    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(44100.0);
    renderer.setInstrument(&probe);

    constexpr int blockSize = 4096;
    std::vector<float> bufferL(blockSize, 0.0f);
    std::vector<float> bufferR(blockSize, 0.0f);
    float* channels[2] = { bufferL.data(), bufferR.data() };

    for (int i = 0; i < 8; ++i) {
        const SampleCount pos = transport.advance(blockSize);
        AudioBlock block { channels, 2, blockSize };
        renderer.process(block, pos);
    }

    REQUIRE_FALSE(probe.calls.empty());

    float previous = -1.0f;
    for (const auto& call : probe.calls) {
        REQUIRE(call.index == 0);
        REQUIRE(call.value >= previous - 1e-6f);
        previous = call.value;
    }
    REQUIRE(previous == Catch::Approx(1.0f).margin(1e-4));
}

TEST_CASE("MidiTrackRenderer never evaluates an automation lane with no points", "[automation]") {
    Track track;
    track.kind = TrackKind::Midi;
    track.automation.push_back(AutomationLaneSlot { 0, AutomationLane() });

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument probe;
    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(44100.0);
    renderer.setInstrument(&probe);

    std::vector<float> bufferL(512, 0.0f);
    std::vector<float> bufferR(512, 0.0f);
    float* channels[2] = { bufferL.data(), bufferR.data() };

    const SampleCount pos = transport.advance(512);
    AudioBlock block { channels, 2, 512 };
    renderer.process(block, pos);

    REQUIRE(probe.calls.empty());
}

TEST_CASE("MidiTrackRenderer never evaluates a lane whose paramIndex is out of range", "[automation]") {
    Track track;
    track.kind = TrackKind::Midi;
    track.automation.push_back(AutomationLaneSlot { 99, AutomationLane() });
    track.automation[0].lane.addPoint(AutomationPoint { 0, 0.5f });

    Transport transport;
    transport.setTempo(120.0);
    transport.play();

    ProbeInstrument probe; // numParameters() == 4, index 99 is out of range
    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(44100.0);
    renderer.setInstrument(&probe);

    std::vector<float> bufferL(512, 0.0f);
    std::vector<float> bufferR(512, 0.0f);
    float* channels[2] = { bufferL.data(), bufferR.data() };

    const SampleCount pos = transport.advance(512);
    AudioBlock block { channels, 2, 512 };
    renderer.process(block, pos);

    REQUIRE(probe.calls.empty());
}

TEST_CASE("MidiTrackRenderer never evaluates automation while the transport is stopped", "[automation]") {
    Track track;
    track.kind = TrackKind::Midi;
    track.automation.push_back(AutomationLaneSlot { 0, AutomationLane() });
    track.automation[0].lane.addPoint(AutomationPoint { 0, 0.5f });
    track.automation[0].lane.addPoint(AutomationPoint { 960, 1.0f });

    Transport transport;
    transport.setTempo(120.0); // never played

    ProbeInstrument probe;
    MidiTrackRenderer renderer(transport, track);
    renderer.prepare(44100.0);
    renderer.setInstrument(&probe);

    std::vector<float> bufferL(512, 0.0f);
    std::vector<float> bufferR(512, 0.0f);
    float* channels[2] = { bufferL.data(), bufferR.data() };

    const SampleCount pos = transport.advance(512);
    AudioBlock block { channels, 2, 512 };
    renderer.process(block, pos);

    REQUIRE(probe.calls.empty());
}

TEST_CASE("AddAutomationLaneCommand appends a lane, undo removes it", "[automation]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    AddAutomationLaneCommand command(arrangement, trackIndex, 2);
    command.execute();
    REQUIRE(arrangement.track(trackIndex).automation.size() == 1);
    REQUIRE(arrangement.track(trackIndex).automation[0].paramIndex == 2);

    command.undo();
    REQUIRE(arrangement.track(trackIndex).automation.empty());
}

TEST_CASE("RemoveAutomationLaneCommand removes a lane, undo restores it at the same index", "[automation]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);
    arrangement.track(trackIndex).automation.push_back(AutomationLaneSlot { 0, AutomationLane() });
    arrangement.track(trackIndex).automation.push_back(AutomationLaneSlot { 1, AutomationLane() });

    RemoveAutomationLaneCommand command(arrangement, trackIndex, 0);
    command.execute();
    REQUIRE(arrangement.track(trackIndex).automation.size() == 1);
    REQUIRE(arrangement.track(trackIndex).automation[0].paramIndex == 1);

    command.undo();
    REQUIRE(arrangement.track(trackIndex).automation.size() == 2);
    REQUIRE(arrangement.track(trackIndex).automation[0].paramIndex == 0);
    REQUIRE(arrangement.track(trackIndex).automation[1].paramIndex == 1);
}

TEST_CASE("AddAutomationPointCommand adds a point, undo removes it by exact match", "[automation]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);
    arrangement.track(trackIndex).automation.push_back(AutomationLaneSlot { 0, AutomationLane() });

    AddAutomationPointCommand command(arrangement, trackIndex, 0, AutomationPoint { 100, 0.5f });
    command.execute();
    REQUIRE(arrangement.track(trackIndex).automation[0].lane.points().size() == 1);

    command.undo();
    REQUIRE(arrangement.track(trackIndex).automation[0].lane.points().empty());
}

TEST_CASE("RemoveAutomationPointCommand removes a point, undo re-adds it", "[automation]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);
    arrangement.track(trackIndex).automation.push_back(AutomationLaneSlot { 0, AutomationLane() });
    arrangement.track(trackIndex).automation[0].lane.addPoint(AutomationPoint { 100, 0.5f });

    RemoveAutomationPointCommand command(arrangement, trackIndex, 0, 0);
    command.execute();
    REQUIRE(arrangement.track(trackIndex).automation[0].lane.points().empty());

    command.undo();
    const auto& points = arrangement.track(trackIndex).automation[0].lane.points();
    REQUIRE(points.size() == 1);
    REQUIRE(points[0].tick == 100);
    REQUIRE(points[0].value == Catch::Approx(0.5f));
}

TEST_CASE("MoveAutomationPointCommand moves a point, undo restores the original", "[automation]") {
    Arrangement arrangement;
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);
    arrangement.track(trackIndex).automation.push_back(AutomationLaneSlot { 0, AutomationLane() });
    arrangement.track(trackIndex).automation[0].lane.addPoint(AutomationPoint { 100, 0.5f });

    MoveAutomationPointCommand command(arrangement, trackIndex, 0,
        AutomationPoint { 100, 0.5f }, AutomationPoint { 200, 0.8f });
    command.execute();

    {
        const auto& points = arrangement.track(trackIndex).automation[0].lane.points();
        REQUIRE(points.size() == 1);
        REQUIRE(points[0].tick == 200);
        REQUIRE(points[0].value == Catch::Approx(0.8f));
    }

    command.undo();

    {
        const auto& points = arrangement.track(trackIndex).automation[0].lane.points();
        REQUIRE(points.size() == 1);
        REQUIRE(points[0].tick == 100);
        REQUIRE(points[0].value == Catch::Approx(0.5f));
    }
}
