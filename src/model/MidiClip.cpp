// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: an ordered collection of notes with a fixed length in ticks

#include "model/MidiClip.h"

#include <algorithm>

namespace hearth::model {

// Inserts note keeping notes() sorted by startTick
void MidiClip::addNote(const Note& note) {
    const auto insertPos = std::upper_bound(m_notes.begin(), m_notes.end(), note,
        [](const Note& a, const Note& b) {
            return a.startTick < b.startTick;
        });
    m_notes.insert(insertPos, note);
}

// Removes the note at index, index must be within notes().size()
void MidiClip::removeNoteAt(std::size_t index) {
    m_notes.erase(m_notes.begin() + static_cast<std::ptrdiff_t>(index));
}

// Returns all notes, sorted by startTick
const std::vector<Note>& MidiClip::notes() const {
    return m_notes;
}

// Returns the clip length in ticks
int64_t MidiClip::lengthTicks() const {
    return m_lengthTicks;
}

// Sets the clip length in ticks
void MidiClip::setLengthTicks(int64_t length) {
    m_lengthTicks = length;
}

} // namespace hearth::model
