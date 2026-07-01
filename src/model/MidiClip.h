// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: an ordered collection of notes with a fixed length in ticks

#pragma once

#include "model/Note.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace howl::model {

class MidiClip {
public:
    // Inserts note keeping notes() sorted by startTick
    void addNote(const Note& note);

    // Removes the note at index, index must be within notes().size()
    void removeNoteAt(std::size_t index);

    // Replaces the note at index with note, re-sorted by startTick, returns its new index
    std::size_t replaceNoteAt(std::size_t index, const Note& note);

    // Returns all notes, sorted by startTick
    const std::vector<Note>& notes() const;

    // Returns the clip length in ticks
    int64_t lengthTicks() const;

    // Sets the clip length in ticks
    void setLengthTicks(int64_t length);

private:
    std::vector<Note> m_notes;
    int64_t m_lengthTicks = 0;
};

} // namespace howl::model
