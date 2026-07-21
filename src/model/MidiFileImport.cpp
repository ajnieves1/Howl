// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: reads a Standard MIDI File into one MidiClip at the project tick resolution

#include "model/MidiFileImport.h"

#include "model/Note.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace howl::model {

// Reads a Standard MIDI File and flattens every track into one MidiClip at
// kTicksPerQuarter resolution, returns nullopt when the file will not parse or
// carries no notes
std::optional<MidiClip> importMidiFileAsClip(const juce::File& file) {
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) {
        return std::nullopt;
    }

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(stream)) {
        return std::nullopt;
    }

    // A positive time format is ticks per quarter note, SMPTE (negative) is not supported yet
    const short timeFormat = midiFile.getTimeFormat();
    if (timeFormat <= 0) {
        return std::nullopt;
    }
    const int64_t filePpq = timeFormat;

    MidiClip clip;
    int64_t lastNoteEndTick = 0;

    const int numTracks = midiFile.getNumTracks();
    for (int trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        const juce::MidiMessageSequence* sourceTrack = midiFile.getTrack(trackIndex);
        if (sourceTrack == nullptr) {
            continue;
        }

        // A read track does not pair note offs to note ons on its own, a mutable copy does
        juce::MidiMessageSequence track(*sourceTrack);
        track.updateMatchedPairs();

        for (int eventIndex = 0; eventIndex < track.getNumEvents(); ++eventIndex) {
            const juce::MidiMessageSequence::MidiEventHolder* holder = track.getEventPointer(eventIndex);
            if (holder == nullptr || !holder->message.isNoteOn() || holder->noteOffObject == nullptr) {
                continue;
            }

            const double startTicksInFile = holder->message.getTimeStamp();
            const double endTicksInFile = holder->noteOffObject->message.getTimeStamp();

            const int64_t startTick = static_cast<int64_t>(startTicksInFile) * kTicksPerQuarter / filePpq;
            const int64_t endTick = static_cast<int64_t>(endTicksInFile) * kTicksPerQuarter / filePpq;
            const int64_t lengthTick = std::max<int64_t>(1, endTick - startTick);

            Note note;
            note.key = holder->message.getNoteNumber();
            note.velocity = holder->message.getFloatVelocity();
            note.startTick = startTick;
            note.lengthTicks = lengthTick;
            clip.addNote(note);

            lastNoteEndTick = std::max(lastNoteEndTick, startTick + lengthTick);
        }
    }

    if (clip.notes().empty()) {
        return std::nullopt;
    }

    // Round the clip length up to a whole bar so it tiles cleanly on the timeline
    constexpr int64_t ticksPerBar = kTicksPerQuarter * 4;
    const int64_t bars = std::max<int64_t>(1, (lastNoteEndTick + ticksPerBar - 1) / ticksPerBar);
    clip.setLengthTicks(bars * ticksPerBar);

    return clip;
}

} // namespace howl::model
