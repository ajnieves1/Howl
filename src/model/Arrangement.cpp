// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: tracks holding clips placed at tick positions on the timeline

#include "model/Arrangement.h"

#include <algorithm>

namespace hearth::model {

// Adds an empty track of the given kind, returns its index
std::size_t Arrangement::addTrack(const std::string& name, TrackKind kind) {
    m_tracks.push_back(Track { name, kind, {}, {} });
    return m_tracks.size() - 1;
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

// Inserts placement into track(trackIndex).midiClips, kept sorted by startTick
void Arrangement::addMidiClipPlacement(std::size_t trackIndex, const MidiClipPlacement& placement) {
    auto& placements = m_tracks[trackIndex].midiClips;
    const auto insertPos = std::upper_bound(placements.begin(), placements.end(), placement,
        [](const MidiClipPlacement& a, const MidiClipPlacement& b) {
            return a.startTick < b.startTick;
        });
    placements.insert(insertPos, placement);
}

// Inserts placement into track(trackIndex).audioClips, kept sorted by startTick
void Arrangement::addAudioClipPlacement(std::size_t trackIndex, const AudioClipPlacement& placement) {
    auto& placements = m_tracks[trackIndex].audioClips;
    const auto insertPos = std::upper_bound(placements.begin(), placements.end(), placement,
        [](const AudioClipPlacement& a, const AudioClipPlacement& b) {
            return a.startTick < b.startTick;
        });
    placements.insert(insertPos, placement);
}

} // namespace hearth::model
