// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: concrete reversible edits to an Arrangement

#include "model/Commands.h"

#include <utility>

namespace howl::model {

// Stores the arrangement, track, and placement to add on execute()
AddMidiClipCommand::AddMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex, MidiClipPlacement placement)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placement(std::move(placement))
{
}

// Inserts the stored placement, remembers where it landed
void AddMidiClipCommand::execute() {
    m_placementIndex = m_arrangement.addMidiClipPlacement(m_trackIndex, m_placement);
}

// Removes the placement this command added
void AddMidiClipCommand::undo() {
    m_arrangement.removeMidiClipPlacementAt(m_trackIndex, m_placementIndex);
}

// Stores the arrangement, track, and placement index to remove on execute()
RemoveMidiClipCommand::RemoveMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placementIndex(placementIndex)
{
}

// Remembers the placement's data, then removes it
void RemoveMidiClipCommand::execute() {
    m_removedPlacement = m_arrangement.track(m_trackIndex).midiClips[m_placementIndex];
    m_arrangement.removeMidiClipPlacementAt(m_trackIndex, m_placementIndex);
}

// Re-adds the removed placement, remembers its new index
void RemoveMidiClipCommand::undo() {
    m_placementIndex = m_arrangement.addMidiClipPlacement(m_trackIndex, m_removedPlacement);
}

// Stores the arrangement, track, placement index, and destination tick
MoveMidiClipCommand::MoveMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex,
                                          int64_t newStartTick)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placementIndex(placementIndex)
    , m_newStartTick(newStartTick)
{
}

// Remembers the current tick, moves the placement to the new tick
void MoveMidiClipCommand::execute() {
    m_oldStartTick = m_arrangement.track(m_trackIndex).midiClips[m_placementIndex].startTick;
    m_placementIndex = m_arrangement.moveMidiClipPlacementAt(m_trackIndex, m_placementIndex, m_newStartTick);
}

// Moves the placement back to the tick it had before execute()
void MoveMidiClipCommand::undo() {
    m_placementIndex = m_arrangement.moveMidiClipPlacementAt(m_trackIndex, m_placementIndex, m_oldStartTick);
}

// Stores the arrangement, track, placement index, and destination tick
MoveAudioClipCommand::MoveAudioClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex,
                                            int64_t newStartTick)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placementIndex(placementIndex)
    , m_newStartTick(newStartTick)
{
}

// Remembers the current tick, moves the placement to the new tick
void MoveAudioClipCommand::execute() {
    m_oldStartTick = m_arrangement.track(m_trackIndex).audioClips[m_placementIndex].startTick;
    m_placementIndex = m_arrangement.moveAudioClipPlacementAt(m_trackIndex, m_placementIndex, m_newStartTick);
}

// Moves the placement back to the tick it had before execute()
void MoveAudioClipCommand::undo() {
    m_placementIndex = m_arrangement.moveAudioClipPlacementAt(m_trackIndex, m_placementIndex, m_oldStartTick);
}

// Stores the arrangement, track, placement, and note to add on execute()
AddNoteCommand::AddNoteCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex, Note note)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placementIndex(placementIndex)
    , m_note(note)
{
}

// Inserts the stored note into the placed clip, remembers where it landed
void AddNoteCommand::execute() {
    MidiClip& clip = m_arrangement.track(m_trackIndex).midiClips[m_placementIndex].clip;
    m_noteIndex = clip.addNote(m_note);
}

// Removes the note this command added
void AddNoteCommand::undo() {
    MidiClip& clip = m_arrangement.track(m_trackIndex).midiClips[m_placementIndex].clip;
    clip.removeNoteAt(m_noteIndex);
}

// Stores the arrangement, track, placement, and note index to remove on execute()
RemoveNoteCommand::RemoveNoteCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex,
                                      std::size_t noteIndex)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placementIndex(placementIndex)
    , m_noteIndex(noteIndex)
{
}

// Remembers the note's data, then removes it
void RemoveNoteCommand::execute() {
    MidiClip& clip = m_arrangement.track(m_trackIndex).midiClips[m_placementIndex].clip;
    m_removedNote = clip.notes()[m_noteIndex];
    clip.removeNoteAt(m_noteIndex);
}

// Re-adds the removed note, remembers its new index
void RemoveNoteCommand::undo() {
    MidiClip& clip = m_arrangement.track(m_trackIndex).midiClips[m_placementIndex].clip;
    m_noteIndex = clip.addNote(m_removedNote);
}

} // namespace howl::model
