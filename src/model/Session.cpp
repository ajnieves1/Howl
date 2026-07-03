// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the session launch grid, one slot column per arrangement track, one row per scene

#include "model/Session.h"

#include <utility>

namespace howl::model {

// Returns the number of scenes
std::size_t Session::numScenes() const {
    return m_numScenes;
}

// Appends an empty scene row to every column, returns the new scene index
std::size_t Session::addScene() {
    for (auto& column : m_columns) {
        column.push_back(ClipSlot {});
    }

    return m_numScenes++;
}

// Returns the number of track columns
std::size_t Session::numTracks() const {
    return m_columns.size();
}

// Appends an empty column holding numScenes empty slots
void Session::addTrackColumn() {
    m_columns.push_back(std::vector<ClipSlot>(m_numScenes));
}

// Inserts a column at index, undo support
void Session::insertTrackColumn(std::size_t index, std::vector<ClipSlot> column) {
    m_columns.insert(m_columns.begin() + static_cast<std::ptrdiff_t>(index), std::move(column));
}

// Removes and returns the column at index, undo support
std::vector<ClipSlot> Session::removeTrackColumn(std::size_t index) {
    std::vector<ClipSlot> column = std::move(m_columns[index]);
    m_columns.erase(m_columns.begin() + static_cast<std::ptrdiff_t>(index));
    return column;
}

// Returns the slot at trackIndex, sceneIndex
ClipSlot& Session::slot(std::size_t trackIndex, std::size_t sceneIndex) {
    return m_columns[trackIndex][sceneIndex];
}

// Read-only slot access
const ClipSlot& Session::slot(std::size_t trackIndex, std::size_t sceneIndex) const {
    return m_columns[trackIndex][sceneIndex];
}

} // namespace howl::model
