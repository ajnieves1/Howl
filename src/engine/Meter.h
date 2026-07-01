// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: peak and RMS metering, readings flow to the UI via a lock-free queue

#pragma once

#include "core/LockFreeQueue.h"
#include "core/Types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace howl::engine {

struct MeterReading {
    float peak;
    float rms;
};

class Meter {
public:
    // [RT] Computes peak and RMS for the block and pushes a reading, drops it if the queue is full
    void processBlock(const AudioBlock& audio) noexcept {
        float peak = 0.0f;
        double sumSquares = 0.0;
        int sampleCount = 0;

        for (int channel = 0; channel < audio.numChannels; ++channel) {
            for (int frame = 0; frame < audio.numFrames; ++frame) {
                const float sample = audio.channels[channel][frame];
                peak = std::max(peak, std::abs(sample));
                sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
                ++sampleCount;
            }
        }

        const float rms = sampleCount > 0
            ? static_cast<float>(std::sqrt(sumSquares / sampleCount))
            : 0.0f;

        m_readings.push({ peak, rms });
    }

    // Pops the oldest pending reading, returns false if none are pending
    bool popReading(MeterReading& out) noexcept {
        return m_readings.pop(out);
    }

private:
    static constexpr std::size_t kQueueCapacity = 64;

    LockFreeQueue<MeterReading, kQueueCapacity> m_readings;
};

} // namespace howl::engine
