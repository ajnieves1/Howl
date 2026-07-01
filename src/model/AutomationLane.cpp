// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: automation points evaluated over time, linear interpolation between them

#include "model/AutomationLane.h"

#include <algorithm>

namespace howl::model {

// Inserts point keeping points() sorted by tick
void AutomationLane::addPoint(const AutomationPoint& point) {
    const auto insertPos = std::upper_bound(m_points.begin(), m_points.end(), point,
        [](const AutomationPoint& a, const AutomationPoint& b) {
            return a.tick < b.tick;
        });
    m_points.insert(insertPos, point);
}

// Removes the point at index, index must be within points().size()
void AutomationLane::removePointAt(std::size_t index) {
    m_points.erase(m_points.begin() + static_cast<std::ptrdiff_t>(index));
}

// Returns all points, sorted by tick
const std::vector<AutomationPoint>& AutomationLane::points() const {
    return m_points;
}

// Linear interpolation, clamps to the first or last point outside the range, 0.5 if empty
float AutomationLane::valueAtTick(int64_t tick) const {
    if (m_points.empty()) {
        return 0.5f;
    }

    if (tick <= m_points.front().tick) {
        return m_points.front().value;
    }
    if (tick >= m_points.back().tick) {
        return m_points.back().value;
    }

    // The two clamps above guarantee upperIt lands strictly inside the vector
    const auto upperIt = std::upper_bound(m_points.begin(), m_points.end(), tick,
        [](int64_t value, const AutomationPoint& point) {
            return value < point.tick;
        });
    const AutomationPoint& upper = *upperIt;
    const AutomationPoint& lower = *(upperIt - 1);

    const int64_t span = upper.tick - lower.tick;
    if (span <= 0) {
        return upper.value;
    }

    const float ratio = static_cast<float>(tick - lower.tick) / static_cast<float>(span);
    return lower.value + (upper.value - lower.value) * ratio;
}

} // namespace howl::model
