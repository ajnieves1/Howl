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

// Takes the mixer, the target strip, and ownership of the effect to add
AddEffectCommand::AddEffectCommand(Mixer& mixer, StripAddress strip, std::unique_ptr<engine::Effect> effect)
    : m_mixer(mixer)
    , m_strip(strip)
    , m_effect(std::move(effect))
{
}

// Moves the effect into the chain, then recomputes PDC latencies
void AddEffectCommand::execute() {
    engine::EffectChain& chain = m_mixer.strip(m_strip).effects();
    m_index = chain.size();
    chain.add(std::move(m_effect));
    m_mixer.updateLatencies();
}

// Takes the effect back out of the chain, holds it, then recomputes PDC latencies
void AddEffectCommand::undo() {
    engine::EffectChain& chain = m_mixer.strip(m_strip).effects();
    m_effect = chain.takeAt(m_index);
    m_mixer.updateLatencies();
}

// Stores the mixer, the target strip, and the effect index to remove on execute()
RemoveEffectCommand::RemoveEffectCommand(Mixer& mixer, StripAddress strip, std::size_t effectIndex)
    : m_mixer(mixer)
    , m_strip(strip)
    , m_index(effectIndex)
{
}

// Takes the effect out of the chain and holds it, then recomputes PDC latencies
void RemoveEffectCommand::execute() {
    engine::EffectChain& chain = m_mixer.strip(m_strip).effects();
    m_effect = chain.takeAt(m_index);
    m_mixer.updateLatencies();
}

// Re-inserts the held effect at its original index, then recomputes PDC latencies
void RemoveEffectCommand::undo() {
    engine::EffectChain& chain = m_mixer.strip(m_strip).effects();
    chain.insertAt(m_index, std::move(m_effect));
    m_mixer.updateLatencies();
}

// Stores the mixer, track, and send to add on execute()
AddSendCommand::AddSendCommand(Mixer& mixer, std::size_t trackIndex, Send send)
    : m_mixer(mixer)
    , m_trackIndex(trackIndex)
    , m_send(send)
{
}

// Adds the stored send to the track
void AddSendCommand::execute() {
    m_mixer.addSend(m_trackIndex, m_send);
}

// Removes the send this command added, it is always the last one
void AddSendCommand::undo() {
    const std::size_t lastIndex = m_mixer.sends(m_trackIndex).size() - 1;
    m_mixer.removeSend(m_trackIndex, lastIndex);
}

// Stores the mixer, track, and send index to remove on execute()
RemoveSendCommand::RemoveSendCommand(Mixer& mixer, std::size_t trackIndex, std::size_t sendIndex)
    : m_mixer(mixer)
    , m_trackIndex(trackIndex)
    , m_sendIndex(sendIndex)
{
}

// Remembers the send's data, then removes it
void RemoveSendCommand::execute() {
    m_removedSend = m_mixer.sends(m_trackIndex)[m_sendIndex];
    m_mixer.removeSend(m_trackIndex, m_sendIndex);
}

// Re-adds the removed send at the end
void RemoveSendCommand::undo() {
    m_mixer.addSend(m_trackIndex, m_removedSend);
}

// Stores the arrangement, mixer, and the new track's name and kind
AddTrackCommand::AddTrackCommand(Arrangement& arrangement, Mixer& mixer, std::string name, TrackKind kind)
    : m_arrangement(arrangement)
    , m_mixer(mixer)
    , m_name(std::move(name))
    , m_kind(kind)
{
}

// arrangement.addTrack + mixer.insertTrackStrip at the new index
void AddTrackCommand::execute() {
    m_index = m_arrangement.addTrack(m_name, m_kind);
    m_mixer.insertTrackStrip(m_index);
}

// arrangement.removeTrack + mixer.removeTrackStrip
void AddTrackCommand::undo() {
    m_arrangement.removeTrack(m_index);
    m_mixer.removeTrackStrip(m_index);
}

// Stores the arrangement, mixer, and the track index to remove on execute()
RemoveTrackCommand::RemoveTrackCommand(Arrangement& arrangement, Mixer& mixer, std::size_t trackIndex)
    : m_arrangement(arrangement)
    , m_mixer(mixer)
    , m_index(trackIndex)
{
}

// Copies the Track (Track is copyable by design), then removes both
void RemoveTrackCommand::execute() {
    m_removedTrack = m_arrangement.track(m_index);
    m_arrangement.removeTrack(m_index);
    m_mixer.removeTrackStrip(m_index);
}

// arrangement.insertTrack(index, copy) + mixer.insertTrackStrip(index)
void RemoveTrackCommand::undo() {
    m_arrangement.insertTrack(m_index, m_removedTrack);
    m_mixer.insertTrackStrip(m_index);
}

} // namespace howl::model
