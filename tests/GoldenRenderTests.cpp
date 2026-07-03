// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: offline render of a fixed audio-clip arrangement matches a committed golden wav

#include "dsp/SubtractiveSynth.h"
#include "engine/Transport.h"
#include "io/AudioFile.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"
#include "model/MidiClip.h"
#include "model/Note.h"
#include "model/OfflineRenderer.h"
#include "model/TrackFreezer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using howl::AudioBlock;
using howl::SampleCount;
using howl::dsp::SubtractiveSynth;
using howl::engine::Transport;
using howl::io::AudioFileReader;
using howl::model::Arrangement;
using howl::model::ArrangementNode;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::kTicksPerQuarter;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Note;
using howl::model::OfflineRenderer;
using howl::model::TrackFreezer;
using howl::model::TrackKind;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kNumChannels = 2;
constexpr int kBlockSize = 256;
constexpr int kNumFrames = 512;

// Builds the same two-track, audio-clip-only arrangement every time, no
// transcendental math anywhere in its content or its rendering path, which
// is what makes the golden render bit-portable across platforms
Arrangement buildTestArrangement() {
    Arrangement arrangement;
    const std::size_t trackA = arrangement.addTrack("A", TrackKind::Audio);
    const std::size_t trackB = arrangement.addTrack("B", TrackKind::Audio);

    AudioClip clipA(
        std::vector<std::vector<float>> {
            { 0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.40f, 0.30f, 0.20f, 0.10f, 0.00f },
            { -0.10f, -0.20f, -0.30f, -0.40f, -0.50f, -0.40f, -0.30f, -0.20f, -0.10f, 0.00f }
        },
        kSampleRate);

    AudioClip clipB(
        std::vector<std::vector<float>> {
            { 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f },
            { 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f }
        },
        kSampleRate);

    arrangement.addAudioClipPlacement(trackA, AudioClipPlacement { 0, clipA });
    arrangement.addAudioClipPlacement(trackB, AudioClipPlacement { 0, clipB });

    return arrangement;
}

} // namespace

TEST_CASE("Offline render of a fixed audio-clip arrangement matches the committed golden wav", "[golden]") {
    Arrangement arrangement = buildTestArrangement();
    Transport transport;

    const std::filesystem::path renderPath = std::filesystem::temp_directory_path() / "howl_golden_render_test.wav";

    REQUIRE(OfflineRenderer::renderToFile(arrangement, transport, kSampleRate, kBlockSize, kNumChannels,
                                          kNumFrames, renderPath.string()));

    const std::filesystem::path goldenPath =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / "golden_render.wav";

    AudioFileReader rendered;
    REQUIRE(rendered.open(renderPath.string()));

    AudioFileReader golden;
    REQUIRE(golden.open(goldenPath.string()));

    REQUIRE(rendered.lengthInSamples() == golden.lengthInSamples());

    const int totalFrames = static_cast<int>(rendered.lengthInSamples());
    std::vector<float> renderedLeft(static_cast<std::size_t>(totalFrames));
    std::vector<float> renderedRight(static_cast<std::size_t>(totalFrames));
    std::vector<float> goldenLeft(static_cast<std::size_t>(totalFrames));
    std::vector<float> goldenRight(static_cast<std::size_t>(totalFrames));

    float* renderedChannels[2] = { renderedLeft.data(), renderedRight.data() };
    float* goldenChannels[2] = { goldenLeft.data(), goldenRight.data() };

    AudioBlock renderedBlock { renderedChannels, 2, totalFrames };
    AudioBlock goldenBlock { goldenChannels, 2, totalFrames };

    rendered.read(renderedBlock, 0);
    golden.read(goldenBlock, 0);

    const float tolerance = 0.001f;
    for (int i = 0; i < totalFrames; ++i) {
        REQUIRE(std::abs(renderedLeft[static_cast<std::size_t>(i)] - goldenLeft[static_cast<std::size_t>(i)]) < tolerance);
        REQUIRE(std::abs(renderedRight[static_cast<std::size_t>(i)] - goldenRight[static_cast<std::size_t>(i)]) < tolerance);
    }

    std::filesystem::remove(renderPath);
}

namespace {

// Builds a one-track MIDI arrangement with one held note starting at tick 0
Arrangement buildMidiArrangement(std::size_t& trackIndex) {
    Arrangement arrangement;
    trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);

    MidiClip clip;
    clip.setLengthTicks(kTicksPerQuarter * 4);
    clip.addNote(Note { 60, 0.8f, 0, kTicksPerQuarter * 4 });

    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, clip });
    return arrangement;
}

// Reads a whole wav into per-channel vectors
void readWholeFile(const std::filesystem::path& path, int numChannels, std::vector<std::vector<float>>& out) {
    AudioFileReader reader;
    REQUIRE(reader.open(path.string()));

    const int totalFrames = static_cast<int>(reader.lengthInSamples());
    out.assign(static_cast<std::size_t>(numChannels), std::vector<float>(static_cast<std::size_t>(totalFrames)));

    std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
    for (int channel = 0; channel < numChannels; ++channel) {
        channelPointers[static_cast<std::size_t>(channel)] = out[static_cast<std::size_t>(channel)].data();
    }

    AudioBlock block { channelPointers.data(), numChannels, totalFrames };
    reader.read(block, 0);
}

} // namespace

TEST_CASE("OfflineRenderer::renderNodeToFile renders a MIDI clip through an instrument to a playable wav", "[offlinerenderer]") {
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    constexpr int numChannels = 2;
    constexpr SampleCount lengthSamples = 4096;

    std::size_t trackIndex = 0;
    Arrangement arrangement = buildMidiArrangement(trackIndex);
    Transport transport;
    transport.setTempo(120.0);

    ArrangementNode node(transport, arrangement);
    node.prepare(sampleRate, blockSize, numChannels);

    SubtractiveSynth synth;
    synth.prepare(sampleRate, blockSize);
    node.setInstrumentForTrack(trackIndex, &synth);

    const std::filesystem::path renderPath =
        std::filesystem::temp_directory_path() / "howl_export_test.wav";

    REQUIRE(OfflineRenderer::renderNodeToFile(node, transport, sampleRate, blockSize, numChannels,
                                              lengthSamples, juce::File(renderPath.string())));

    std::vector<std::vector<float>> rendered;
    readWholeFile(renderPath, numChannels, rendered);

    REQUIRE(rendered[0].size() == static_cast<std::size_t>(lengthSamples));

    bool foundNonSilence = false;
    for (float sample : rendered[0]) {
        if (std::abs(sample) > 1e-6f) {
            foundNonSilence = true;
            break;
        }
    }
    REQUIRE(foundNonSilence);

    std::filesystem::remove(renderPath);
}

TEST_CASE("OfflineRenderer::renderNodeToFile is deterministic across two renders of the same content", "[offlinerenderer]") {
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    constexpr int numChannels = 2;
    constexpr SampleCount lengthSamples = 4096;

    std::size_t trackIndex = 0;
    Arrangement arrangement = buildMidiArrangement(trackIndex);
    Transport transport;
    transport.setTempo(120.0);

    ArrangementNode node(transport, arrangement);
    node.prepare(sampleRate, blockSize, numChannels);

    const std::filesystem::path pathA = std::filesystem::temp_directory_path() / "howl_export_test_a.wav";
    const std::filesystem::path pathB = std::filesystem::temp_directory_path() / "howl_export_test_b.wav";

    SubtractiveSynth synthA;
    synthA.prepare(sampleRate, blockSize);
    node.setInstrumentForTrack(trackIndex, &synthA);
    REQUIRE(OfflineRenderer::renderNodeToFile(node, transport, sampleRate, blockSize, numChannels,
                                              lengthSamples, juce::File(pathA.string())));

    SubtractiveSynth synthB;
    synthB.prepare(sampleRate, blockSize);
    node.setInstrumentForTrack(trackIndex, &synthB);
    REQUIRE(OfflineRenderer::renderNodeToFile(node, transport, sampleRate, blockSize, numChannels,
                                              lengthSamples, juce::File(pathB.string())));

    std::vector<std::vector<float>> renderedA;
    std::vector<std::vector<float>> renderedB;
    readWholeFile(pathA, numChannels, renderedA);
    readWholeFile(pathB, numChannels, renderedB);

    REQUIRE(renderedA[0].size() == renderedB[0].size());
    for (std::size_t i = 0; i < renderedA[0].size(); ++i) {
        REQUIRE(renderedA[0][i] == renderedB[0][i]);
        REQUIRE(renderedA[1][i] == renderedB[1][i]);
    }

    std::filesystem::remove(pathA);
    std::filesystem::remove(pathB);
}

TEST_CASE("OfflineRenderer::renderNodeToFile renders a frozen track the same as live within tolerance", "[offlinerenderer]") {
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    constexpr int numChannels = 2;
    constexpr SampleCount lengthSamples = 4096;

    std::size_t trackIndex = 0;
    Arrangement arrangement = buildMidiArrangement(trackIndex);
    Transport transport;
    transport.setTempo(120.0);

    ArrangementNode node(transport, arrangement);
    node.prepare(sampleRate, blockSize, numChannels);

    const std::filesystem::path livePath = std::filesystem::temp_directory_path() / "howl_export_live.wav";
    const std::filesystem::path frozenPath = std::filesystem::temp_directory_path() / "howl_export_frozen.wav";

    SubtractiveSynth liveSynth;
    liveSynth.prepare(sampleRate, blockSize);
    node.setInstrumentForTrack(trackIndex, &liveSynth);
    REQUIRE(OfflineRenderer::renderNodeToFile(node, transport, sampleRate, blockSize, numChannels,
                                              lengthSamples, juce::File(livePath.string())));

    SubtractiveSynth freezeSynth;
    freezeSynth.prepare(sampleRate, blockSize);
    auto frozenBuffer = TrackFreezer::renderTrack(arrangement, node.mixer(), transport, trackIndex,
        &freezeSynth, sampleRate, blockSize, numChannels);
    REQUIRE_FALSE(frozenBuffer.empty());

    node.setFrozen(trackIndex, std::move(frozenBuffer));
    REQUIRE(OfflineRenderer::renderNodeToFile(node, transport, sampleRate, blockSize, numChannels,
                                              lengthSamples, juce::File(frozenPath.string())));

    std::vector<std::vector<float>> live;
    std::vector<std::vector<float>> frozen;
    readWholeFile(livePath, numChannels, live);
    readWholeFile(frozenPath, numChannels, frozen);

    REQUIRE(live[0].size() == frozen[0].size());

    const float tolerance = 0.001f;
    for (std::size_t i = 0; i < live[0].size(); ++i) {
        REQUIRE(std::abs(live[0][i] - frozen[0][i]) < tolerance);
        REQUIRE(std::abs(live[1][i] - frozen[1][i]) < tolerance);
    }

    std::filesystem::remove(livePath);
    std::filesystem::remove(frozenPath);
}
