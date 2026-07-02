// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: concrete reversible edits to an Arrangement

#pragma once

#include "engine/Effect.h"
#include "model/Arrangement.h"
#include "model/Command.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "model/Note.h"

#include <cstddef>
#include <cstdint>
#include <memory>

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

// Moves an audio clip placement to a new start tick, undo restores the old one
class MoveAudioClipCommand : public Command {
public:
    // Stores the arrangement, track, placement index, and destination tick
    MoveAudioClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex,
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

// Adds a created effect to a strip's chain, undo takes it back out and holds it
class AddEffectCommand : public Command {
public:
    // Takes the mixer, the target strip, and ownership of the effect to add
    AddEffectCommand(Mixer& mixer, StripAddress strip, std::unique_ptr<engine::Effect> effect);

    // Moves the effect into the chain, then recomputes PDC latencies
    void execute() override;

    // Takes the effect back out of the chain, holds it, then recomputes PDC latencies
    void undo() override;

private:
    Mixer& m_mixer;
    StripAddress m_strip;
    std::unique_ptr<engine::Effect> m_effect;
    std::size_t m_index = 0;
};

// Removes the effect at index from a strip's chain, undo re-inserts it at the same index
class RemoveEffectCommand : public Command {
public:
    // Stores the mixer, the target strip, and the effect index to remove on execute()
    RemoveEffectCommand(Mixer& mixer, StripAddress strip, std::size_t effectIndex);

    // Takes the effect out of the chain and holds it, then recomputes PDC latencies
    void execute() override;

    // Re-inserts the held effect at its original index, then recomputes PDC latencies
    void undo() override;

private:
    Mixer& m_mixer;
    StripAddress m_strip;
    std::size_t m_index;
    std::unique_ptr<engine::Effect> m_effect;
};

// Adds a send from a track to a bus, undo removes it
class AddSendCommand : public Command {
public:
    // Stores the mixer, track, and send to add on execute()
    AddSendCommand(Mixer& mixer, std::size_t trackIndex, Send send);

    void execute() override;
    void undo() override;

private:
    Mixer& m_mixer;
    std::size_t m_trackIndex;
    Send m_send;
};

// Removes the send at index from a track, undo re-adds it (at the end, order is
// irrelevant, sends only accumulate)
class RemoveSendCommand : public Command {
public:
    // Stores the mixer, track, and send index to remove on execute()
    RemoveSendCommand(Mixer& mixer, std::size_t trackIndex, std::size_t sendIndex);

    void execute() override;
    void undo() override;

private:
    Mixer& m_mixer;
    std::size_t m_trackIndex;
    std::size_t m_sendIndex;
    Send m_removedSend {};
};

} // namespace howl::model
