// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: tracks holding clips placed at tick positions on the timeline

#pragma once

#include "model/AudioClip.h"
#include "model/MidiClip.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace howl::model {

// A MIDI clip placed on the timeline at startTick
struct MidiClipPlacement {
    int64_t startTick;
    MidiClip clip;
};

// An audio clip placed on the timeline at startTick, converted to samples at render time
struct AudioClipPlacement {
    int64_t startTick;
    AudioClip clip;
};

enum class TrackKind {
    Midi,
    Audio
};

// A track holds placements matching its own kind, the other kind's vector stays empty
struct Track {
    std::string name;
    TrackKind kind;
    std::vector<MidiClipPlacement> midiClips;
    std::vector<AudioClipPlacement> audioClips;
};

class Arrangement {
public:
    // Adds an empty track of the given kind, returns its index
    std::size_t addTrack(const std::string& name, TrackKind kind);

    // Returns the track at index
    Track& track(std::size_t index);

    // Returns the track at index
    const Track& track(std::size_t index) const;

    // Returns the number of tracks
    std::size_t numTracks() const;

    // Inserts placement into track(trackIndex).midiClips, kept sorted by startTick
    void addMidiClipPlacement(std::size_t trackIndex, const MidiClipPlacement& placement);

    // Inserts placement into track(trackIndex).audioClips, kept sorted by startTick
    void addAudioClipPlacement(std::size_t trackIndex, const AudioClipPlacement& placement);

private:
    std::vector<Track> m_tracks;
};

} // namespace howl::model
