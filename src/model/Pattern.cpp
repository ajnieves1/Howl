// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: PatternBank storage, track column sync, and placement bookkeeping

#include "model/Pattern.h"

#include "model/Note.h"

#include <algorithm>

namespace howl::model {

namespace {
constexpr int64_t kMinPatternLengthTicks = kTicksPerQuarter * 4;
}

// Returns the number of patterns
std::size_t PatternBank::numPatterns() const {
    return m_patterns.size();
}

// Appends a pattern with one empty lane per track, returns its index
std::size_t PatternBank::addPattern(const std::string& name, std::size_t numTracks) {
    const std::size_t index = m_patterns.size();
    m_patterns.push_back(Pattern { name, std::vector<MidiClip>(numTracks) });
    return index;
}

// Returns the pattern at index
Pattern& PatternBank::pattern(std::size_t index) {
    return m_patterns[index];
}

// Read only pattern access
const Pattern& PatternBank::pattern(std::size_t index) const {
    return m_patterns[index];
}

// Returns the max lane clip length, minimum one bar of 3840 ticks
int64_t PatternBank::patternLengthTicks(std::size_t index) const {
    int64_t length = kMinPatternLengthTicks;
    for (const MidiClip& lane : m_patterns[index].trackClips) {
        length = std::max(length, lane.lengthTicks());
    }
    return length;
}

// Appends an empty lane to every pattern, track add
void PatternBank::addTrackColumn() {
    for (Pattern& pattern : m_patterns) {
        pattern.trackClips.emplace_back();
    }
}

// Inserts a lane at trackIndex into every pattern, undo support
void PatternBank::insertTrackColumn(std::size_t trackIndex, std::vector<MidiClip> lanes) {
    for (std::size_t i = 0; i < m_patterns.size(); ++i) {
        m_patterns[i].trackClips.insert(
            m_patterns[i].trackClips.begin() + static_cast<std::ptrdiff_t>(trackIndex), lanes[i]);
    }
}

// Removes and returns the lane at trackIndex from every pattern, undo support
std::vector<MidiClip> PatternBank::removeTrackColumn(std::size_t trackIndex) {
    std::vector<MidiClip> lanes;
    lanes.reserve(m_patterns.size());
    for (Pattern& pattern : m_patterns) {
        lanes.push_back(pattern.trackClips[trackIndex]);
        pattern.trackClips.erase(pattern.trackClips.begin() + static_cast<std::ptrdiff_t>(trackIndex));
    }
    return lanes;
}

// Returns all placements
const std::vector<PatternPlacement>& PatternBank::placements() const {
    return m_placements;
}

// Appends a placement, returns its index
std::size_t PatternBank::addPlacement(const PatternPlacement& placement) {
    const std::size_t index = m_placements.size();
    m_placements.push_back(placement);
    return index;
}

// Removes and returns the placement at index
PatternPlacement PatternBank::removePlacementAt(std::size_t index) {
    PatternPlacement removed = m_placements[index];
    m_placements.erase(m_placements.begin() + static_cast<std::ptrdiff_t>(index));
    return removed;
}

// Replaces the placement at index, for moves
void PatternBank::replacePlacementAt(std::size_t index, const PatternPlacement& placement) {
    m_placements[index] = placement;
}

} // namespace howl::model
