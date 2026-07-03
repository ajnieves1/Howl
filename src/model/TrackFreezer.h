// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: offline-renders one track's timeline content through its instrument and strip FX

#pragma once

#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/Mixer.h"

#include <cstddef>
#include <vector>

namespace howl::model {

class TrackFreezer {
public:
    // Renders track trackIndex from sample 0 to its last clip end plus one second of tail,
    // returns per-channel buffers, empty when the track has no content, caller pauses the device
    static std::vector<std::vector<float>> renderTrack(Arrangement& arrangement, Mixer& mixer,
                                                         engine::Transport& transport, std::size_t trackIndex,
                                                         engine::Instrument* instrument, double sampleRate,
                                                         int blockSize, int numChannels);
};

} // namespace howl::model
