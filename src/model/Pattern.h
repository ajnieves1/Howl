// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a named multi-track note container and its bank of timeline placements

#pragma once

#include "model/MidiClip.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace howl::model {

// One named multi-track note container, one lane clip per arrangement track
struct Pattern {
    std::string name;
    // Indexed by track, empty clips for uninvolved tracks
    std::vector<MidiClip> trackClips;
};

// A pattern placed on the arrange timeline, plays once from startTick
struct PatternPlacement {
    std::size_t patternIndex;
    int64_t startTick;
};

// Owns every pattern and their timeline placements
class PatternBank {
public:
    // Returns the number of patterns
    std::size_t numPatterns() const;

    // Appends a pattern with one empty lane per track, returns its index
    std::size_t addPattern(const std::string& name, std::size_t numTracks);

    // Returns the pattern at index
    Pattern& pattern(std::size_t index);

    // Read only pattern access
    const Pattern& pattern(std::size_t index) const;

    // Returns the max lane clip length, minimum one bar of 3840 ticks
    int64_t patternLengthTicks(std::size_t index) const;

    // Appends an empty lane to every pattern, track add
    void addTrackColumn();

    // Inserts a lane at trackIndex into every pattern, undo support
    void insertTrackColumn(std::size_t trackIndex, std::vector<MidiClip> lanes);

    // Removes and returns the lane at trackIndex from every pattern, undo support
    std::vector<MidiClip> removeTrackColumn(std::size_t trackIndex);

    // Returns all placements
    const std::vector<PatternPlacement>& placements() const;

    // Appends a placement, returns its index
    std::size_t addPlacement(const PatternPlacement& placement);

    // Removes and returns the placement at index
    PatternPlacement removePlacementAt(std::size_t index);

    // Replaces the placement at index, for moves
    void replacePlacementAt(std::size_t index, const PatternPlacement& placement);

private:
    std::vector<Pattern> m_patterns;
    std::vector<PatternPlacement> m_placements;
};

} // namespace howl::model
