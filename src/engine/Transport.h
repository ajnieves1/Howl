// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: sample-accurate playhead with play, stop, loop, and tempo

#pragma once

#include "core/Types.h"

#include <atomic>

namespace hearth::engine {

class Transport {
public:
    // Allocates the initial, disabled loop region snapshot
    Transport();

    // Frees the current loop region snapshot
    ~Transport();

    // Starts the playhead advancing
    void play();

    // Stops the playhead advancing
    void stop();

    // Sets the tempo in beats per minute
    void setTempo(double bpm);

    // Sets the loop region in samples, applied the next time advance() reaches loopEnd
    void setLoop(SampleCount start, SampleCount end, bool enabled);

    // [RT] Advances the playhead by numFrames, returns the playhead at block start
    SampleCount advance(int numFrames) noexcept;

    // Reads the current playhead position
    SampleCount position() const noexcept;

private:
    // Immutable once published, so a single atomic load in advance() always
    // sees a self-consistent start/end/enabled triple, never a torn mix
    struct LoopRegion {
        SampleCount start;
        SampleCount end;
        bool enabled;
    };

    std::atomic<bool> m_playing { false };
    std::atomic<double> m_tempo { 120.0 };
    std::atomic<SampleCount> m_position { 0 };
    std::atomic<const LoopRegion*> m_loopRegion;
};

} // namespace hearth::engine
