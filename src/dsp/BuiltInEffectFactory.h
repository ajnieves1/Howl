// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: creates the built-in dsp effects behind engine::IEffectFactory

#pragma once

#include "engine/EffectFactory.h"

namespace howl::dsp {

class BuiltInEffectFactory : public engine::IEffectFactory {
public:
    // Returns all six types in enum order
    std::vector<engine::EffectType> availableTypes() const override;

    // Returns the fixed display name for a type
    const char* displayName(engine::EffectType type) const override;

    // Switches on the type and news up the matching effect
    std::unique_ptr<engine::Effect> create(engine::EffectType type) const override;
};

} // namespace howl::dsp
