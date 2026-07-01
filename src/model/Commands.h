// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: concrete reversible edits to an Arrangement

#pragma once

#include "model/Arrangement.h"
#include "model/Command.h"
#include "model/MidiClip.h"
#include "model/Note.h"

#include <cstddef>
#include <cstdint>

namespace howl::model {

// Adds a MIDI clip placement to a track, undo removes it
class AddMidiClipCommand : public Command {
public:
    // Stores the arrangement, track, and placement to add on execute()
    AddMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex, MidiClipPlacement placement);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    MidiClipPlacement m_placement;
    std::size_t m_placementIndex = 0;
};

// Removes a MIDI clip placement from a track, undo re-adds it
class RemoveMidiClipCommand : public Command {
public:
    // Stores the arrangement, track, and placement index to remove on execute()
    RemoveMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
    MidiClipPlacement m_removedPlacement;
};

// Moves a MIDI clip placement to a new start tick, undo restores the old one
class MoveMidiClipCommand : public Command {
public:
    // Stores the arrangement, track, placement index, and destination tick
    MoveMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex,
                         int64_t newStartTick);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
    int64_t m_newStartTick;
    int64_t m_oldStartTick = 0;
};

// Adds a note to a placed MIDI clip, undo removes it
class AddNoteCommand : public Command {
public:
    // Stores the arrangement, track, placement, and note to add on execute()
    AddNoteCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex, Note note);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
    Note m_note;
    std::size_t m_noteIndex = 0;
};

// Removes a note from a placed MIDI clip, undo re-adds it
class RemoveNoteCommand : public Command {
public:
    // Stores the arrangement, track, placement, and note index to remove on execute()
    RemoveNoteCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex,
                       std::size_t noteIndex);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
    std::size_t m_noteIndex;
    Note m_removedNote {};
};

} // namespace howl::model
