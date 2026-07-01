// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per-track channel strips summed to a master strip

#pragma once

#include "core/Types.h"
#include "model/ChannelStrip.h"

#include <cstddef>
#include <vector>

namespace howl::model {

class Mixer {
public:
    // Sizes the mixer to numTracks track strips plus a master strip
    void prepare(std::size_t numTracks, double sampleRate, int maxBlockSize, int numChannels);

    // Returns the channel strip for the given track
    ChannelStrip& trackStrip(std::size_t trackIndex);

    // Returns the master channel strip
    ChannelStrip& masterStrip();

    // [RT] Applies each track strip, sums soloed/unmuted tracks, applies master, into output
    void process(const std::vector<AudioBlock>& trackBuffers, AudioBlock& output, SampleCount pos) noexcept;

private:
    std::vector<ChannelStrip> m_trackStrips;
    ChannelStrip m_masterStrip;
};

} // namespace howl::model
