// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: a single MIDI note stored in clip-relative ticks

#pragma once

#include <cstdint>

namespace hearth::model {

// Ticks per quarter note, fixed project-wide
constexpr int kTicksPerQuarter = 960;

struct Note {
    int key;
    float velocity;
    int64_t startTick;
    int64_t lengthTicks;
};

} // namespace hearth::model
