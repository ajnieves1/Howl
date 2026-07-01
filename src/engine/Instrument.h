// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the interface every built-in instrument implements

#pragma once

#include "core/Types.h"

namespace howl::engine {

class Instrument {
public:
    // Allows deleting through an Instrument pointer
    virtual ~Instrument() = default;

    // Prepares the instrument to render at the given rate and block size
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // [RT] Starts a note, key is a MIDI note number, velocity is 0..1
    virtual void noteOn(int key, float velocity) noexcept = 0;

    // [RT] Releases a note by key
    virtual void noteOff(int key) noexcept = 0;

    // [RT] Renders active voices into audio, overwriting the block
    virtual void render(AudioBlock& audio) noexcept = 0;
};

} // namespace howl::engine
