// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: creates built-in effects by type without the UI depending on dsp

#pragma once

#include "engine/Effect.h"

#include <memory>
#include <vector>

namespace howl::engine {

// Every built-in effect type, order here is display order
enum class EffectType {
    Gain,
    Equalizer,
    Compressor,
    Limiter,
    Delay,
    Reverb
};

class IEffectFactory {
public:
    virtual ~IEffectFactory() = default;

    // Returns every type this factory can create, in display order
    virtual std::vector<EffectType> availableTypes() const = 0;

    // Returns the fixed human-readable name for a type
    virtual const char* displayName(EffectType type) const = 0;

    // Creates a fresh, unprepared instance of the type
    virtual std::unique_ptr<Effect> create(EffectType type) const = 0;
};

} // namespace howl::engine
