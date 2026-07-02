// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: BuiltInEffectFactory type coverage and name-wiring tests

#include "dsp/BuiltInEffectFactory.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using howl::dsp::BuiltInEffectFactory;
using howl::engine::EffectType;

TEST_CASE("BuiltInEffectFactory reports exactly six types in enum order", "[effectfactory]") {
    BuiltInEffectFactory factory;
    const auto types = factory.availableTypes();

    REQUIRE(types.size() == 6);
    REQUIRE(types[0] == EffectType::Gain);
    REQUIRE(types[1] == EffectType::Equalizer);
    REQUIRE(types[2] == EffectType::Compressor);
    REQUIRE(types[3] == EffectType::Limiter);
    REQUIRE(types[4] == EffectType::Delay);
    REQUIRE(types[5] == EffectType::Reverb);
}

TEST_CASE("BuiltInEffectFactory creates a non-null instance whose displayName matches for every type", "[effectfactory]") {
    BuiltInEffectFactory factory;

    for (EffectType type : factory.availableTypes()) {
        auto effect = factory.create(type);
        REQUIRE(effect != nullptr);
        REQUIRE(std::string(effect->displayName()) == std::string(factory.displayName(type)));
    }
}
