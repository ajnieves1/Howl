// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: automation points evaluated over time, linear interpolation between them

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace howl::model {

struct AutomationPoint {
    int64_t tick;
    float value;
};

class AutomationLane {
public:
    // Inserts point keeping points() sorted by tick
    void addPoint(const AutomationPoint& point);

    // Removes the point at index, index must be within points().size()
    void removePointAt(std::size_t index);

    // Returns all points, sorted by tick
    const std::vector<AutomationPoint>& points() const;

    // Linear interpolation, clamps to the first or last point outside the range, 0.5 if empty
    float valueAtTick(int64_t tick) const;

private:
    std::vector<AutomationPoint> m_points;
};

} // namespace howl::model
