// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: creates the built-in dsp effects behind engine::IEffectFactory

#include "dsp/BuiltInEffectFactory.h"

#include "dsp/Compressor.h"
#include "dsp/Delay.h"
#include "dsp/Equalizer.h"
#include "dsp/GainEffect.h"
#include "dsp/Limiter.h"
#include "dsp/Reverb.h"

namespace howl::dsp {

// Returns all six types in enum order
std::vector<engine::EffectType> BuiltInEffectFactory::availableTypes() const {
    return {
        engine::EffectType::Gain,
        engine::EffectType::Equalizer,
        engine::EffectType::Compressor,
        engine::EffectType::Limiter,
        engine::EffectType::Delay,
        engine::EffectType::Reverb
    };
}

// Returns the fixed display name for a type
const char* BuiltInEffectFactory::displayName(engine::EffectType type) const {
    switch (type) {
        case engine::EffectType::Gain: return "Gain";
        case engine::EffectType::Equalizer: return "EQ";
        case engine::EffectType::Compressor: return "Compressor";
        case engine::EffectType::Limiter: return "Limiter";
        case engine::EffectType::Delay: return "Delay";
        case engine::EffectType::Reverb: return "Reverb";
        default: return "";
    }
}

// Switches on the type and news up the matching effect
std::unique_ptr<engine::Effect> BuiltInEffectFactory::create(engine::EffectType type) const {
    switch (type) {
        case engine::EffectType::Gain: return std::make_unique<GainEffect>();
        case engine::EffectType::Equalizer: return std::make_unique<Equalizer>();
        case engine::EffectType::Compressor: return std::make_unique<Compressor>();
        case engine::EffectType::Limiter: return std::make_unique<Limiter>();
        case engine::EffectType::Delay: return std::make_unique<Delay>();
        case engine::EffectType::Reverb: return std::make_unique<Reverb>();
        default: return nullptr;
    }
}

} // namespace howl::dsp
