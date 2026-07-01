// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: offline render of a fixed audio-clip arrangement matches a committed golden wav

#include "engine/Transport.h"
#include "io/AudioFile.h"
#include "model/Arrangement.h"
#include "model/OfflineRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using howl::AudioBlock;
using howl::engine::Transport;
using howl::io::AudioFileReader;
using howl::model::Arrangement;
using howl::model::AudioClip;
using howl::model::AudioClipPlacement;
using howl::model::OfflineRenderer;
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
