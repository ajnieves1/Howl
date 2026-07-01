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

    // Returns the number of automatable parameters
    virtual int numParameters() const = 0;

    // Returns the display name of the parameter at index
    virtual const char* parameterName(int index) const = 0;

    // [RT] Sets a parameter by index, value is normalized 0..1
    virtual void setParameter(int index, float value) noexcept = 0;
};

} // namespace howl::engine
