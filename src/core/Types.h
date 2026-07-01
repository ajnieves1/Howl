// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: fundamental time and audio block types shared by all modules

#pragma once

#include <cstdint>

namespace howl {

using SampleCount = int64_t;
using ChannelCount = int;

struct AudioBlock {
    float** channels;
    ChannelCount numChannels;
    int numFrames;
};

} // namespace howl
