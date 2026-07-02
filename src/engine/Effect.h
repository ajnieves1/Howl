// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the format-agnostic interface every audio effect implements

#pragma once

#include "core/Types.h"

namespace howl::engine {

struct EffectParamInfo {
    int index;
    const char* name;
};

class Effect {
public:
    // Allows deleting through an Effect pointer
    virtual ~Effect() = default;

    // Prepares the effect to run at the given rate and block size
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // [RT] Transforms the block in place
    virtual void process(AudioBlock& audio) noexcept = 0;

    // Clears any internal state, tails, or delay lines
    virtual void reset() noexcept = 0;

    // Reports processing latency in samples, 0 if none
    virtual int latencySamples() const noexcept = 0;

    // Returns the number of automatable parameters
    virtual int numParameters() const = 0;

    // Returns the display name of the parameter at index
    virtual const char* parameterName(int index) const = 0;

    // [RT] Sets a parameter by index, value is normalized 0..1
    virtual void setParameter(int index, float value) noexcept = 0;

    // Returns the last normalized 0..1 value set for the param at index, its default before any set
    virtual float getParameter(int index) const noexcept = 0;

    // Fixed human-readable effect name for UI lists
    virtual const char* displayName() const noexcept = 0;
};

} // namespace howl::engine
