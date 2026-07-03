// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: offline time-stretch over plain buffers, pitch preserved, never called on the audio thread

#include "dsp/OfflineStretcher.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace howl::dsp {

// Returns channels stretched by timeRatio (2.0 = twice as long), empty on invalid input
std::vector<std::vector<float>> OfflineStretcher::stretch(const std::vector<std::vector<float>>& channels,
                                                            double sampleRate, double timeRatio) {
    if (channels.empty() || sampleRate <= 0.0 || timeRatio <= 0.0) {
        return {};
    }

    const auto numChannels = channels.size();
    const std::size_t length = channels[0].size();
    for (const auto& channel : channels) {
        if (channel.size() != length) {
            return {};
        }
    }

    if (std::abs(timeRatio - 1.0) < 1e-6) {
        return channels;
    }

    RubberBand::RubberBandStretcher stretcher(static_cast<std::size_t>(sampleRate), numChannels,
        RubberBand::RubberBandStretcher::OptionProcessOffline | RubberBand::RubberBandStretcher::OptionEngineFiner,
        timeRatio);
    stretcher.setExpectedInputDuration(length);

    std::vector<const float*> inputPointers(numChannels);
    for (std::size_t c = 0; c < numChannels; ++c) {
        inputPointers[c] = channels[c].data();
    }

    constexpr std::size_t kChunkFrames = 4096;

    std::size_t studied = 0;
    while (studied < length) {
        const std::size_t chunk = std::min(kChunkFrames, length - studied);
        std::vector<const float*> chunkPointers(numChannels);
        for (std::size_t c = 0; c < numChannels; ++c) {
            chunkPointers[c] = inputPointers[c] + studied;
        }
        studied += chunk;
        stretcher.study(chunkPointers.data(), chunk, studied >= length);
    }

    std::vector<std::vector<float>> output(numChannels);
    std::vector<float> retrieveBuffer(numChannels * kChunkFrames);
    std::vector<float*> retrievePointers(numChannels);
    for (std::size_t c = 0; c < numChannels; ++c) {
        retrievePointers[c] = retrieveBuffer.data() + c * kChunkFrames;
    }

    const auto drainAvailable = [&]() {
        int available = stretcher.available();
        while (available > 0) {
            const auto toRetrieve = static_cast<std::size_t>(std::min<int>(available, static_cast<int>(kChunkFrames)));
            const std::size_t got = stretcher.retrieve(retrievePointers.data(), toRetrieve);
            for (std::size_t c = 0; c < numChannels; ++c) {
                output[c].insert(output[c].end(), retrievePointers[c], retrievePointers[c] + got);
            }
            available = stretcher.available();
        }
    };

    std::size_t processed = 0;
    while (processed < length) {
        const std::size_t chunk = std::min(kChunkFrames, length - processed);
        std::vector<const float*> chunkPointers(numChannels);
        for (std::size_t c = 0; c < numChannels; ++c) {
            chunkPointers[c] = inputPointers[c] + processed;
        }
        processed += chunk;
        stretcher.process(chunkPointers.data(), chunk, processed >= length);
        drainAvailable();
    }

    return output;
}

} // namespace howl::dsp
