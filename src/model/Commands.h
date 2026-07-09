// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: concrete reversible edits to an Arrangement

#pragma once

#include "engine/Effect.h"
#include "model/Arrangement.h"
#include "model/AudioClip.h"
#include "model/AutomationLane.h"
#include "model/Command.h"
#include "model/MidiClip.h"
#include "model/Mixer.h"
#include "model/Note.h"
#include "model/Session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

    // The placement's current index, re-sorted by startTick after execute()/undo(); a group
    // move rebuilds its selection from this rather than assuming the index never changed
    std::size_t placementIndex() const noexcept { return m_placementIndex; }

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

    // The placement's current index, re-sorted by startTick after execute()/undo(); a group
    // move rebuilds its selection from this rather than assuming the index never changed
    std::size_t placementIndex() const noexcept { return m_placementIndex; }

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
    int64_t m_newStartTick;
    int64_t m_oldStartTick = 0;
};

// Sets a placed MIDI clip's length, undo restores the old length
class ResizeMidiClipCommand : public Command {
public:
    // Stores the placement address and both lengths
    ResizeMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex,
                          int64_t oldLengthTicks, int64_t newLengthTicks);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
    int64_t m_oldLengthTicks;
    int64_t m_newLengthTicks;
};

// Flips a placed clip's mute flag, undo flips it back. kind selects which of the
// track's two placement vectors (midiClips or audioClips) placementIndex addresses
class ToggleClipMuteCommand : public Command {
public:
    // Stores the arrangement, clip kind, track, and placement index to flip on execute()
    ToggleClipMuteCommand(Arrangement& arrangement, TrackKind kind, std::size_t trackIndex,
                          std::size_t placementIndex);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    TrackKind m_kind;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
};

// Owns every pattern and their timeline placements, added in a later phase
class PatternBank;

// Where a MidiClip lives, so note commands can re-resolve it at execute/undo time
// rather than storing a pointer that dangles once the owning container reallocates
struct ClipAddress {
    // Which container owns the clip
    enum class Source {
        Arrangement,
        Session,
        Pattern
    };

    Source source;
    std::size_t trackIndex;
    // Placement index for Arrangement, scene index for Session, pattern index for Pattern
    std::size_t slotIndex;
};

// Returns the addressed clip, nullptr when the address no longer resolves. patterns is
// nullptr until a later phase introduces the PatternBank, a Pattern-sourced address
// always fails to resolve until then
MidiClip* resolveClip(Arrangement& arrangement, Session& session, PatternBank* patterns,
                       const ClipAddress& address);

// Adds a note to the addressed clip, undo removes it by exact field match
class AddNoteCommand : public Command {
public:
    // Stores the containers, the clip address, and the note to add on execute()
    AddNoteCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                   ClipAddress address, Note note);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    Session& m_session;
    PatternBank* m_patterns;
    ClipAddress m_address;
    Note m_note;
    bool m_applied = false;
};

// Removes a note matching an exact field match from the addressed clip, undo re-adds it
class RemoveNoteCommand : public Command {
public:
    // Stores the containers, the clip address, and the note to remove on execute()
    RemoveNoteCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                       ClipAddress address, Note note);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    Session& m_session;
    PatternBank* m_patterns;
    ClipAddress m_address;
    Note m_note;
    bool m_applied = false;
};

// Removes every note in before then adds every note in after, both by exact field match,
// tolerating notes already moved by a live drag: a before note already gone is skipped, an
// after note already present is not duplicated. Undo runs the same operation in reverse.
// One command covers a single note move/resize (one element vectors), a group move, deletion
// (after empty), and duplication (before empty)
class ReplaceNotesCommand : public Command {
public:
    // Stores the containers, the clip address, and the before/after note sets
    ReplaceNotesCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                         ClipAddress address, std::vector<Note> before, std::vector<Note> after);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    Session& m_session;
    PatternBank* m_patterns;
    ClipAddress m_address;
    std::vector<Note> m_before;
    std::vector<Note> m_after;
};

// Splits a note in two at splitTick, which must lie strictly inside it, undo restores the original
class SplitNoteCommand : public Command {
public:
    // Stores the containers, the clip address, the note to split, and the split point
    SplitNoteCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                      ClipAddress address, Note original, int64_t splitTick);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    Session& m_session;
    PatternBank* m_patterns;
    ClipAddress m_address;
    Note m_original;
    int64_t m_splitTick;
    bool m_applied = false;
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

// Adds a track to the arrangement, a default strip to the mixer, and a session column,
// undo removes all three
class AddTrackCommand : public Command {
public:
    // Stores the arrangement, mixer, session, and the new track's name and kind
    AddTrackCommand(Arrangement& arrangement, Mixer& mixer, Session& session, std::string name, TrackKind kind);

    // arrangement.addTrack + mixer.insertTrackStrip + session.addTrackColumn, all at the new index
    void execute() override;

    // arrangement.removeTrack + mixer.removeTrackStrip + session.removeTrackColumn
    void undo() override;

private:
    Arrangement& m_arrangement;
    Mixer& m_mixer;
    Session& m_session;
    std::string m_name;
    TrackKind m_kind;
    std::size_t m_index = 0;
};

// Removes a track, its strip, and its session column; undo restores the track copy and
// session column, and a DEFAULT strip (strip contents - FX, gain, sends - are not preserved
// across remove/undo in v1, documented)
class RemoveTrackCommand : public Command {
public:
    // Stores the arrangement, mixer, session, and the track index to remove on execute()
    RemoveTrackCommand(Arrangement& arrangement, Mixer& mixer, Session& session, std::size_t trackIndex);

    // Copies the Track (Track is copyable by design) and session column, then removes all three
    void execute() override;

    // arrangement.insertTrack(index, copy) + mixer.insertTrackStrip(index) + session.insertTrackColumn(index, copy)
    void undo() override;

private:
    Arrangement& m_arrangement;
    Mixer& m_mixer;
    Session& m_session;
    std::size_t m_index;
    Track m_removedTrack {};
    std::vector<ClipSlot> m_removedColumn;
};

// Adds an audio clip placement to a track, undo removes it
class AddAudioClipCommand : public Command {
public:
    // Stores the arrangement, track, and placement to add on execute()
    AddAudioClipCommand(Arrangement& arrangement, std::size_t trackIndex, AudioClipPlacement placement);

    // Inserts the stored placement, remembers where it landed
    void execute() override;

    // Removes the placement this command added
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    AudioClipPlacement m_placement;
    std::size_t m_placementIndex = 0;
};

// Removes an audio clip placement from a track, undo re-adds it
class RemoveAudioClipCommand : public Command {
public:
    // Stores the arrangement, track, and placement index to remove on execute()
    RemoveAudioClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex);

    // Remembers the placement's data, then removes it
    void execute() override;

    // Re-adds the removed placement, remembers its new index
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_placementIndex;
    AudioClipPlacement m_removedPlacement;
};

// Puts a fresh MIDI clip into an empty slot, undo empties it again. No-ops if the slot is
// not empty when executed
class AddSessionMidiClipCommand : public Command {
public:
    // Stores the session, slot address, and clip to add on execute()
    AddSessionMidiClipCommand(Session& session, std::size_t trackIndex, std::size_t sceneIndex, MidiClip clip);

    void execute() override;
    void undo() override;

private:
    Session& m_session;
    std::size_t m_trackIndex;
    std::size_t m_sceneIndex;
    MidiClip m_clip;
    bool m_applied = false;
};

// Puts an audio clip into an empty slot, undo empties it again. No-ops if the slot is not
// empty when executed
class AddSessionAudioClipCommand : public Command {
public:
    // Stores the session, slot address, and clip to add on execute()
    AddSessionAudioClipCommand(Session& session, std::size_t trackIndex, std::size_t sceneIndex, AudioClip clip);

    void execute() override;
    void undo() override;

private:
    Session& m_session;
    std::size_t m_trackIndex;
    std::size_t m_sceneIndex;
    AudioClip m_clip;
    bool m_applied = false;
};

// Empties a slot, undo restores the stored slot copy
class ClearSessionSlotCommand : public Command {
public:
    // Stores the session and slot address to clear on execute()
    ClearSessionSlotCommand(Session& session, std::size_t trackIndex, std::size_t sceneIndex);

    void execute() override;
    void undo() override;

private:
    Session& m_session;
    std::size_t m_trackIndex;
    std::size_t m_sceneIndex;
    ClipSlot m_removedSlot;
};

// Appends an empty automation lane for a parameter to a track, undo pops it
class AddAutomationLaneCommand : public Command {
public:
    // Stores the arrangement, track, and parameter index of the lane to add on execute()
    AddAutomationLaneCommand(Arrangement& arrangement, std::size_t trackIndex, int paramIndex);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    int m_paramIndex;
};

// Removes the automation lane at laneIndex from a track, undo restores it at the same index
class RemoveAutomationLaneCommand : public Command {
public:
    // Stores the arrangement, track, and lane index to remove on execute()
    RemoveAutomationLaneCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t laneIndex);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_laneIndex;
    AutomationLaneSlot m_removedSlot {};
};

// Adds a point to an automation lane, undo removes it by exact tick+value match
class AddAutomationPointCommand : public Command {
public:
    // Stores the arrangement, track, lane index, and point to add on execute()
    AddAutomationPointCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t laneIndex,
                               AutomationPoint point);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_laneIndex;
    AutomationPoint m_point;
};

// Removes the point at pointIndex from an automation lane, undo re-adds it
class RemoveAutomationPointCommand : public Command {
public:
    // Stores the arrangement, track, lane index, and point index to remove on execute()
    RemoveAutomationPointCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t laneIndex,
                                  std::size_t pointIndex);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_laneIndex;
    std::size_t m_pointIndex;
    AutomationPoint m_removedPoint {};
};

// Moves an automation point from oldPoint to newPoint (matched by exact tick+value), undo symmetric
class MoveAutomationPointCommand : public Command {
public:
    // Stores the arrangement, track, lane index, and both endpoints of the move
    MoveAutomationPointCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t laneIndex,
                                AutomationPoint oldPoint, AutomationPoint newPoint);

    void execute() override;
    void undo() override;

private:
    Arrangement& m_arrangement;
    std::size_t m_trackIndex;
    std::size_t m_laneIndex;
    AutomationPoint m_oldPoint;
    AutomationPoint m_newPoint;
};

// Groups child commands into one undo step, executed in order and undone in reverse
class CompositeCommand : public Command {
public:
    // Appends a child, call before the composite is performed
    void add(std::unique_ptr<Command> command);

    // Returns the number of children, an empty composite is a legal no-op
    std::size_t size() const;

    void execute() override;
    void undo() override;

private:
    std::vector<std::unique_ptr<Command>> m_children;
};

} // namespace howl::model
