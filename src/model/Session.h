// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the session launch grid, one slot column per arrangement track, one row per scene

#pragma once

#include "model/AudioClip.h"
#include "model/MidiClip.h"

#include <cstddef>
#include <vector>

namespace howl::model {

// What a slot holds, matching the owning track's kind
enum class SlotContent {
    Empty,
    Midi,
    Audio
};

// One cell of the session grid, copyable plain data
struct ClipSlot {
    SlotContent content = SlotContent::Empty;
    // Used when content == Midi
    MidiClip midiClip;
    // Used when content == Audio
    AudioClip audioClip;
};

// The launch grid, one slot column per arrangement track, one row per scene
class Session {
public:
    // Returns the number of scenes
    std::size_t numScenes() const;

    // Appends an empty scene row to every column, returns the new scene index
    std::size_t addScene();

    // Returns the number of track columns
    std::size_t numTracks() const;

    // Appends an empty column holding numScenes empty slots
    void addTrackColumn();

    // Inserts a column at index, undo support
    void insertTrackColumn(std::size_t index, std::vector<ClipSlot> column);

    // Removes and returns the column at index, undo support
    std::vector<ClipSlot> removeTrackColumn(std::size_t index);

    // Returns the slot at trackIndex, sceneIndex
    ClipSlot& slot(std::size_t trackIndex, std::size_t sceneIndex);

    // Read-only slot access
    const ClipSlot& slot(std::size_t trackIndex, std::size_t sceneIndex) const;

private:
    std::vector<std::vector<ClipSlot>> m_columns;
    std::size_t m_numScenes = 0;
};

} // namespace howl::model
