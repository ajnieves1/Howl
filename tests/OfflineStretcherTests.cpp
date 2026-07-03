// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: OfflineStretcher time-ratio, silence/NaN, and pitch-preservation checks

#include "dsp/OfflineStretcher.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using howl::dsp::OfflineStretcher;

namespace {

// Builds a stereo 440 Hz sine of the given length at the given sample rate
std::vector<std::vector<float>> makeSineStereo(double sampleRate, std::size_t length) {
    std::vector<std::vector<float>> channels(2, std::vector<float>(length, 0.0f));
    constexpr double frequency = 440.0;

    for (std::size_t i = 0; i < length; ++i) {
        const auto sample = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * frequency
            * static_cast<double>(i) / sampleRate));
        channels[0][i] = sample;
        channels[1][i] = sample;
    }

    return channels;
}

// Counts zero crossings in a channel, a coarse proxy for dominant frequency
std::size_t countZeroCrossings(const std::vector<float>& channel) {
    std::size_t count = 0;
    for (std::size_t i = 1; i < channel.size(); ++i) {
        if ((channel[i - 1] < 0.0f) != (channel[i] < 0.0f)) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("OfflineStretcher doubles the length within 10% and preserves pitch within 15%", "[stretcher]") {
    const double sampleRate = 44100.0;
    const auto length = static_cast<std::size_t>(sampleRate * 2.0); // 2 seconds

    const auto input = makeSineStereo(sampleRate, length);
    const auto output = OfflineStretcher::stretch(input, sampleRate, 2.0);

    REQUIRE(output.size() == 2);

    const auto expectedLength = static_cast<double>(length) * 2.0;
    const auto actualLength = static_cast<double>(output[0].size());
    REQUIRE(actualLength > expectedLength * 0.9);
    REQUIRE(actualLength < expectedLength * 1.1);

    double sumSquares = 0.0;
    for (float sample : output[0]) {
        REQUIRE_FALSE(std::isnan(sample));
        REQUIRE_FALSE(std::isinf(sample));
        sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const double rms = std::sqrt(sumSquares / static_cast<double>(output[0].size()));
    REQUIRE(rms > 0.1);

    // Coarse pitch check: zero-crossing rate over the middle half should be close to the input's
    const std::size_t inputQuarter = length / 4;
    const std::size_t inputHalfStart = inputQuarter;
    const std::size_t inputHalfEnd = length - inputQuarter;
    std::vector<float> inputMiddle(input[0].begin() + static_cast<std::ptrdiff_t>(inputHalfStart),
        input[0].begin() + static_cast<std::ptrdiff_t>(inputHalfEnd));
    const double inputRate = static_cast<double>(countZeroCrossings(inputMiddle)) / static_cast<double>(inputMiddle.size());

    const std::size_t outputQuarter = output[0].size() / 4;
    const std::size_t outputHalfStart = outputQuarter;
    const std::size_t outputHalfEnd = output[0].size() - outputQuarter;
    std::vector<float> outputMiddle(output[0].begin() + static_cast<std::ptrdiff_t>(outputHalfStart),
        output[0].begin() + static_cast<std::ptrdiff_t>(outputHalfEnd));
    const double outputRate = static_cast<double>(countZeroCrossings(outputMiddle)) / static_cast<double>(outputMiddle.size());

    REQUIRE(outputRate > inputRate * 0.85);
    REQUIRE(outputRate < inputRate * 1.15);
}

TEST_CASE("OfflineStretcher at ratio 1.0 returns a bit-identical copy", "[stretcher]") {
    const double sampleRate = 44100.0;
    const auto input = makeSineStereo(sampleRate, 4096);

    const auto output = OfflineStretcher::stretch(input, sampleRate, 1.0);

    REQUIRE(output.size() == 2);
    REQUIRE(output[0].size() == input[0].size());
    for (std::size_t i = 0; i < input[0].size(); ++i) {
        REQUIRE(output[0][i] == input[0][i]);
        REQUIRE(output[1][i] == input[1][i]);
    }
}

TEST_CASE("OfflineStretcher returns empty for invalid input", "[stretcher]") {
    const double sampleRate = 44100.0;

    REQUIRE(OfflineStretcher::stretch({}, sampleRate, 2.0).empty());

    const auto input = makeSineStereo(sampleRate, 1024);
    REQUIRE(OfflineStretcher::stretch(input, sampleRate, 0.0).empty());
    REQUIRE(OfflineStretcher::stretch(input, 0.0, 2.0).empty());
}
