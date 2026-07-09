// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: one global snap setting, pure tick rounding shared by every editor

#pragma once

#include "model/Note.h"

#include <cstdint>

namespace howl::model {

// Grid divisions the snap combo offers
enum class SnapDivision {
    Bar,
    Beat,
    HalfBeat,
    Step,
    Off
};

namespace detail {

// Floors toward negative infinity, unlike C++'s truncating division, b is always positive here
inline int64_t floorDiv(int64_t a, int64_t b) {
    const int64_t quotient = a / b;
    const int64_t remainder = a % b;
    return remainder < 0 ? quotient - 1 : quotient;
}

} // namespace detail

// Returns ticks per snap unit: 3840, 960, 480, 240, and 0 for Off
inline int64_t snapUnitTicks(SnapDivision division) {
    switch (division) {
        case SnapDivision::Bar:
            return kTicksPerQuarter * 4;
        case SnapDivision::Beat:
            return kTicksPerQuarter;
        case SnapDivision::HalfBeat:
            return kTicksPerQuarter / 2;
        case SnapDivision::Step:
            return kTicksPerQuarter / 4;
        case SnapDivision::Off:
        default:
            return 0;
    }
}

// Rounds tick to the nearest snap unit, clamped at 0, unchanged for Off. A tick exactly
// halfway between two units rounds up, matching snapTickFloor's own rounding direction
inline int64_t snapTick(int64_t tick, SnapDivision division) {
    const int64_t unit = snapUnitTicks(division);
    if (unit <= 0) {
        return tick;
    }

    const int64_t rounded = detail::floorDiv(tick + unit / 2, unit) * unit;
    return rounded < 0 ? 0 : rounded;
}

// Rounds tick down to a snap unit, clamped at 0, unchanged for Off
inline int64_t snapTickFloor(int64_t tick, SnapDivision division) {
    const int64_t unit = snapUnitTicks(division);
    if (unit <= 0) {
        return tick;
    }

    const int64_t floored = detail::floorDiv(tick, unit) * unit;
    return floored < 0 ? 0 : floored;
}

} // namespace howl::model
