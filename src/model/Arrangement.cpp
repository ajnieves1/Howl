// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: tracks holding clips placed at tick positions on the timeline

#include "model/Arrangement.h"

#include <algorithm>
#include <utility>

namespace howl::model {

// Adds an empty track of the given kind, returns its index
std::size_t Arrangement::addTrack(const std::string& name, TrackKind kind) {
    m_tracks.push_back(Track { name, kind, {}, {}, {} });
    return m_tracks.size() - 1;
}

// Removes the track at index
void Arrangement::removeTrack(std::size_t index) {
    m_tracks.erase(m_tracks.begin() + static_cast<std::ptrdiff_t>(index));
}

// Inserts a track at index (undo support)
void Arrangement::insertTrack(std::size_t index, Track track) {
    m_tracks.insert(m_tracks.begin() + static_cast<std::ptrdiff_t>(index), std::move(track));
}

// Returns the track at index
Track& Arrangement::track(std::size_t index) {
    return m_tracks[index];
}

// Returns the track at index
const Track& Arrangement::track(std::size_t index) const {
    return m_tracks[index];
}

// Returns the number of tracks
std::size_t Arrangement::numTracks() const {
    return m_tracks.size();
}

// Inserts placement into track(trackIndex).midiClips, kept sorted by startTick, returns its index
std::size_t Arrangement::addMidiClipPlacement(std::size_t trackIndex, const MidiClipPlacement& placement) {
    auto& placements = m_tracks[trackIndex].midiClips;
    const auto insertPos = std::upper_bound(placements.begin(), placements.end(), placement,
        [](const MidiClipPlacement& a, const MidiClipPlacement& b) {
            return a.startTick < b.startTick;
        });
    const auto index = static_cast<std::size_t>(insertPos - placements.begin());
    placements.insert(insertPos, placement);
    return index;
}

// Inserts placement into track(trackIndex).audioClips, kept sorted by startTick, returns its index
std::size_t Arrangement::addAudioClipPlacement(std::size_t trackIndex, const AudioClipPlacement& placement) {
    auto& placements = m_tracks[trackIndex].audioClips;
    const auto insertPos = std::upper_bound(placements.begin(), placements.end(), placement,
        [](const AudioClipPlacement& a, const AudioClipPlacement& b) {
            return a.startTick < b.startTick;
        });
    const auto index = static_cast<std::size_t>(insertPos - placements.begin());
    placements.insert(insertPos, placement);
    return index;
}

// Removes the placement at placementIndex from track(trackIndex).midiClips
void Arrangement::removeMidiClipPlacementAt(std::size_t trackIndex, std::size_t placementIndex) {
    auto& placements = m_tracks[trackIndex].midiClips;
    placements.erase(placements.begin() + static_cast<std::ptrdiff_t>(placementIndex));
}

// Removes the placement at placementIndex from track(trackIndex).audioClips
void Arrangement::removeAudioClipPlacementAt(std::size_t trackIndex, std::size_t placementIndex) {
    auto& placements = m_tracks[trackIndex].audioClips;
    placements.erase(placements.begin() + static_cast<std::ptrdiff_t>(placementIndex));
}

// Moves the placement at placementIndex to newStartTick, re-sorted, returns its new index
std::size_t Arrangement::moveMidiClipPlacementAt(std::size_t trackIndex, std::size_t placementIndex, int64_t newStartTick) {
    auto& placements = m_tracks[trackIndex].midiClips;
    MidiClipPlacement placement = placements[placementIndex];
    placement.startTick = newStartTick;
    placements.erase(placements.begin() + static_cast<std::ptrdiff_t>(placementIndex));

    const auto insertPos = std::upper_bound(placements.begin(), placements.end(), placement,
        [](const MidiClipPlacement& a, const MidiClipPlacement& b) {
            return a.startTick < b.startTick;
        });
    const auto newIndex = static_cast<std::size_t>(insertPos - placements.begin());
    placements.insert(insertPos, placement);
    return newIndex;
}

// Moves the placement at placementIndex to newStartTick, re-sorted, returns its new index
std::size_t Arrangement::moveAudioClipPlacementAt(std::size_t trackIndex, std::size_t placementIndex, int64_t newStartTick) {
    auto& placements = m_tracks[trackIndex].audioClips;
    AudioClipPlacement placement = placements[placementIndex];
    placement.startTick = newStartTick;
    placements.erase(placements.begin() + static_cast<std::ptrdiff_t>(placementIndex));

    const auto insertPos = std::upper_bound(placements.begin(), placements.end(), placement,
        [](const AudioClipPlacement& a, const AudioClipPlacement& b) {
            return a.startTick < b.startTick;
        });
    const auto newIndex = static_cast<std::size_t>(insertPos - placements.begin());
    placements.insert(insertPos, placement);
    return newIndex;
}

} // namespace howl::model
