// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: offline time-stretch over plain buffers, pitch preserved, never called on the audio thread

#pragma once

#include <vector>

namespace howl::dsp {

class OfflineStretcher {
public:
    // Returns channels stretched by timeRatio (2.0 = twice as long), empty on invalid input
    static std::vector<std::vector<float>> stretch(const std::vector<std::vector<float>>& channels,
                                                     double sampleRate, double timeRatio);
};

} // namespace howl::dsp
