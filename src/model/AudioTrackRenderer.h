// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders one audio track's placed clips, 1:1 sample copy, no time-stretch

#pragma once

#include "core/Types.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"

namespace howl::model {

class AudioTrackRenderer {
public:
    // Stores references to the transport and track to read placements from
    AudioTrackRenderer(engine::Transport& transport, const Track& track);

    // Sets the sample rate used to convert ticks to sample offsets, call before process()
    void prepare(double sampleRate);

    // [RT] Overwrites audio with the block's timeline range from placed clip source samples
    void process(AudioBlock& audio, SampleCount pos) noexcept;

private:
    engine::Transport& m_transport;
    const Track& m_track;
    double m_sampleRate = 44100.0;
};

} // namespace howl::model
