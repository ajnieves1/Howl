// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: concrete reversible edits to an Arrangement

#include "model/Commands.h"

#include <cstring>
#include <utility>

namespace howl::model {

namespace {

// Bit-exact match for a stored automation value, structured to avoid -Wfloat-equal on ==
bool sameValue(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

// Exact field match between two notes, velocity compared bit-exact like sameValue
bool sameNote(const Note& a, const Note& b) {
    return a.key == b.key && a.startTick == b.startTick && a.lengthTicks == b.lengthTicks
        && sameValue(a.velocity, b.velocity);
}

// Finds note by exact field match, returns its index or notes().size() when absent
std::size_t findNoteIndex(const MidiClip& clip, const Note& note) {
    const auto& notes = clip.notes();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (sameNote(notes[i], note)) {
            return i;
        }
    }
    return notes.size();
}

// Removes note from clip if an exact match is present, otherwise does nothing
void removeNoteIfPresent(MidiClip& clip, const Note& note) {
    const std::size_t index = findNoteIndex(clip, note);
    if (index < clip.notes().size()) {
        clip.removeNoteAt(index);
    }
}

// Adds note to clip unless an exact match is already present
void addNoteIfAbsent(MidiClip& clip, const Note& note) {
    if (findNoteIndex(clip, note) == clip.notes().size()) {
        clip.addNote(note);
    }
}

} // namespace

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

// Stores the placement address and both lengths
ResizeMidiClipCommand::ResizeMidiClipCommand(Arrangement& arrangement, std::size_t trackIndex,
                                              std::size_t placementIndex, int64_t oldLengthTicks, int64_t newLengthTicks)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placementIndex(placementIndex)
    , m_oldLengthTicks(oldLengthTicks)
    , m_newLengthTicks(newLengthTicks)
{
}

// Sets the placement's clip to the new length
void ResizeMidiClipCommand::execute() {
    m_arrangement.track(m_trackIndex).midiClips[m_placementIndex].clip.setLengthTicks(m_newLengthTicks);
}

// Restores the clip's length to what it had before execute()
void ResizeMidiClipCommand::undo() {
    m_arrangement.track(m_trackIndex).midiClips[m_placementIndex].clip.setLengthTicks(m_oldLengthTicks);
}

// Returns the addressed clip, nullptr when the address no longer resolves. patterns is
// nullptr until a later phase introduces the PatternBank, a Pattern-sourced address
// always fails to resolve until then
MidiClip* resolveClip(Arrangement& arrangement, Session& session, PatternBank* patterns,
                       const ClipAddress& address) {
    switch (address.source) {
        case ClipAddress::Source::Arrangement: {
            if (address.trackIndex >= arrangement.numTracks()) {
                return nullptr;
            }
            Track& track = arrangement.track(address.trackIndex);
            if (address.slotIndex >= track.midiClips.size()) {
                return nullptr;
            }
            return &track.midiClips[address.slotIndex].clip;
        }
        case ClipAddress::Source::Session: {
            if (address.trackIndex >= session.numTracks() || address.slotIndex >= session.numScenes()) {
                return nullptr;
            }
            ClipSlot& slot = session.slot(address.trackIndex, address.slotIndex);
            if (slot.content != SlotContent::Midi) {
                return nullptr;
            }
            return &slot.midiClip;
        }
        case ClipAddress::Source::Pattern:
        default:
            // No PatternBank exists yet, a Pattern-sourced address never resolves
            (void)patterns;
            return nullptr;
    }
}

// Stores the containers, the clip address, and the note to add on execute()
AddNoteCommand::AddNoteCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                                ClipAddress address, Note note)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_patterns(patterns)
    , m_address(address)
    , m_note(note)
{
}

// Adds the stored note to the resolved clip, no-op when the address does not resolve
void AddNoteCommand::execute() {
    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        m_applied = false;
        return;
    }

    addNoteIfAbsent(*clip, m_note);
    m_applied = true;
}

// Removes the added note by exact field match, no-op when execute() never applied
void AddNoteCommand::undo() {
    if (!m_applied) {
        return;
    }

    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        return;
    }

    removeNoteIfPresent(*clip, m_note);
}

// Stores the containers, the clip address, and the note to remove on execute()
RemoveNoteCommand::RemoveNoteCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                                      ClipAddress address, Note note)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_patterns(patterns)
    , m_address(address)
    , m_note(note)
{
}

// Removes the note by exact field match, no-op when the address does not resolve or
// the note is not found (already removed by something else)
void RemoveNoteCommand::execute() {
    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        m_applied = false;
        return;
    }

    const std::size_t index = findNoteIndex(*clip, m_note);
    if (index >= clip->notes().size()) {
        m_applied = false;
        return;
    }

    clip->removeNoteAt(index);
    m_applied = true;
}

// Re-adds the removed note, no-op when execute() never applied
void RemoveNoteCommand::undo() {
    if (!m_applied) {
        return;
    }

    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        return;
    }

    addNoteIfAbsent(*clip, m_note);
}

// Stores the containers, the clip address, and the before/after note sets
ReplaceNotesCommand::ReplaceNotesCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                                          ClipAddress address, std::vector<Note> before, std::vector<Note> after)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_patterns(patterns)
    , m_address(address)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

// Removes every before note present, then adds every after note absent. Both steps are
// presence checked rather than unconditional, so calling this on a clip a live drag
// already moved into the after state is a harmless no-op, not a duplicate or a crash
void ReplaceNotesCommand::execute() {
    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        return;
    }

    for (const Note& note : m_before) {
        removeNoteIfPresent(*clip, note);
    }
    for (const Note& note : m_after) {
        addNoteIfAbsent(*clip, note);
    }
}

// Runs the same presence checked swap in reverse: removes after notes, restores before notes
void ReplaceNotesCommand::undo() {
    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        return;
    }

    for (const Note& note : m_after) {
        removeNoteIfPresent(*clip, note);
    }
    for (const Note& note : m_before) {
        addNoteIfAbsent(*clip, note);
    }
}

// Stores the containers, the clip address, the note to split, and the split point
SplitNoteCommand::SplitNoteCommand(Arrangement& arrangement, Session& session, PatternBank* patterns,
                                    ClipAddress address, Note original, int64_t splitTick)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_patterns(patterns)
    , m_address(address)
    , m_original(original)
    , m_splitTick(splitTick)
{
}

// Removes the original note and adds its two halves, no-op when the address does not
// resolve, the split point does not lie strictly inside the note, or the note is not found
void SplitNoteCommand::execute() {
    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        m_applied = false;
        return;
    }

    if (m_splitTick <= m_original.startTick || m_splitTick >= m_original.startTick + m_original.lengthTicks) {
        m_applied = false;
        return;
    }

    if (findNoteIndex(*clip, m_original) >= clip->notes().size()) {
        m_applied = false;
        return;
    }

    removeNoteIfPresent(*clip, m_original);
    addNoteIfAbsent(*clip, Note { m_original.key, m_original.velocity, m_original.startTick,
        m_splitTick - m_original.startTick });
    addNoteIfAbsent(*clip, Note { m_original.key, m_original.velocity, m_splitTick,
        m_original.startTick + m_original.lengthTicks - m_splitTick });
    m_applied = true;
}

// Removes both halves and restores the original note, no-op when execute() never applied
void SplitNoteCommand::undo() {
    if (!m_applied) {
        return;
    }

    MidiClip* clip = resolveClip(m_arrangement, m_session, m_patterns, m_address);
    if (clip == nullptr) {
        return;
    }

    removeNoteIfPresent(*clip, Note { m_original.key, m_original.velocity, m_original.startTick,
        m_splitTick - m_original.startTick });
    removeNoteIfPresent(*clip, Note { m_original.key, m_original.velocity, m_splitTick,
        m_original.startTick + m_original.lengthTicks - m_splitTick });
    addNoteIfAbsent(*clip, m_original);
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

// Stores the arrangement, mixer, session, and the new track's name and kind
AddTrackCommand::AddTrackCommand(Arrangement& arrangement, Mixer& mixer, Session& session, std::string name,
                                  TrackKind kind)
    : m_arrangement(arrangement)
    , m_mixer(mixer)
    , m_session(session)
    , m_name(std::move(name))
    , m_kind(kind)
{
}

// arrangement.addTrack + mixer.insertTrackStrip + session.addTrackColumn, all at the new index
void AddTrackCommand::execute() {
    m_index = m_arrangement.addTrack(m_name, m_kind);
    m_mixer.insertTrackStrip(m_index);
    m_session.addTrackColumn();
}

// arrangement.removeTrack + mixer.removeTrackStrip + session.removeTrackColumn
void AddTrackCommand::undo() {
    m_arrangement.removeTrack(m_index);
    m_mixer.removeTrackStrip(m_index);
    m_session.removeTrackColumn(m_index);
}

// Stores the arrangement, mixer, session, and the track index to remove on execute()
RemoveTrackCommand::RemoveTrackCommand(Arrangement& arrangement, Mixer& mixer, Session& session, std::size_t trackIndex)
    : m_arrangement(arrangement)
    , m_mixer(mixer)
    , m_session(session)
    , m_index(trackIndex)
{
}

// Copies the Track (Track is copyable by design) and session column, then removes all three
void RemoveTrackCommand::execute() {
    m_removedTrack = m_arrangement.track(m_index);
    m_removedColumn = m_session.removeTrackColumn(m_index);
    m_arrangement.removeTrack(m_index);
    m_mixer.removeTrackStrip(m_index);
}

// arrangement.insertTrack(index, copy) + mixer.insertTrackStrip(index) + session.insertTrackColumn(index, copy)
void RemoveTrackCommand::undo() {
    m_arrangement.insertTrack(m_index, m_removedTrack);
    m_mixer.insertTrackStrip(m_index);
    m_session.insertTrackColumn(m_index, m_removedColumn);
}

// Stores the arrangement, track, and placement to add on execute()
AddAudioClipCommand::AddAudioClipCommand(Arrangement& arrangement, std::size_t trackIndex, AudioClipPlacement placement)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placement(std::move(placement))
{
}

// Inserts the stored placement, remembers where it landed
void AddAudioClipCommand::execute() {
    m_placementIndex = m_arrangement.addAudioClipPlacement(m_trackIndex, m_placement);
}

// Removes the placement this command added
void AddAudioClipCommand::undo() {
    m_arrangement.removeAudioClipPlacementAt(m_trackIndex, m_placementIndex);
}

// Stores the arrangement, track, and placement index to remove on execute()
RemoveAudioClipCommand::RemoveAudioClipCommand(Arrangement& arrangement, std::size_t trackIndex, std::size_t placementIndex)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_placementIndex(placementIndex)
    , m_removedPlacement { 0, AudioClip({}, 44100.0) }
{
}

// Remembers the placement's data, then removes it
void RemoveAudioClipCommand::execute() {
    m_removedPlacement = m_arrangement.track(m_trackIndex).audioClips[m_placementIndex];
    m_arrangement.removeAudioClipPlacementAt(m_trackIndex, m_placementIndex);
}

// Re-adds the removed placement, remembers its new index
void RemoveAudioClipCommand::undo() {
    m_placementIndex = m_arrangement.addAudioClipPlacement(m_trackIndex, m_removedPlacement);
}

// Stores the session, slot address, and clip to add on execute()
AddSessionMidiClipCommand::AddSessionMidiClipCommand(Session& session, std::size_t trackIndex,
                                                      std::size_t sceneIndex, MidiClip clip)
    : m_session(session)
    , m_trackIndex(trackIndex)
    , m_sceneIndex(sceneIndex)
    , m_clip(std::move(clip))
{
}

// Fills the slot when it is empty, no-ops otherwise
void AddSessionMidiClipCommand::execute() {
    ClipSlot& slot = m_session.slot(m_trackIndex, m_sceneIndex);
    if (slot.content != SlotContent::Empty) {
        m_applied = false;
        return;
    }

    slot.content = SlotContent::Midi;
    slot.midiClip = m_clip;
    m_applied = true;
}

// Empties the slot again, only if this command actually filled it
void AddSessionMidiClipCommand::undo() {
    if (!m_applied) {
        return;
    }

    ClipSlot& slot = m_session.slot(m_trackIndex, m_sceneIndex);
    slot.content = SlotContent::Empty;
    slot.midiClip = MidiClip();
    m_applied = false;
}

// Stores the session, slot address, and clip to add on execute()
AddSessionAudioClipCommand::AddSessionAudioClipCommand(Session& session, std::size_t trackIndex,
                                                        std::size_t sceneIndex, AudioClip clip)
    : m_session(session)
    , m_trackIndex(trackIndex)
    , m_sceneIndex(sceneIndex)
    , m_clip(std::move(clip))
{
}

// Fills the slot when it is empty, no-ops otherwise
void AddSessionAudioClipCommand::execute() {
    ClipSlot& slot = m_session.slot(m_trackIndex, m_sceneIndex);
    if (slot.content != SlotContent::Empty) {
        m_applied = false;
        return;
    }

    slot.content = SlotContent::Audio;
    slot.audioClip = m_clip;
    m_applied = true;
}

// Empties the slot again, only if this command actually filled it
void AddSessionAudioClipCommand::undo() {
    if (!m_applied) {
        return;
    }

    ClipSlot& slot = m_session.slot(m_trackIndex, m_sceneIndex);
    slot.content = SlotContent::Empty;
    slot.audioClip = AudioClip();
    m_applied = false;
}

// Stores the session and slot address to clear on execute()
ClearSessionSlotCommand::ClearSessionSlotCommand(Session& session, std::size_t trackIndex, std::size_t sceneIndex)
    : m_session(session)
    , m_trackIndex(trackIndex)
    , m_sceneIndex(sceneIndex)
{
}

// Remembers the slot's contents, then empties it
void ClearSessionSlotCommand::execute() {
    m_removedSlot = m_session.slot(m_trackIndex, m_sceneIndex);
    m_session.slot(m_trackIndex, m_sceneIndex) = ClipSlot {};
}

// Restores the stored slot copy
void ClearSessionSlotCommand::undo() {
    m_session.slot(m_trackIndex, m_sceneIndex) = m_removedSlot;
}

// Stores the arrangement, track, and parameter index of the lane to add on execute()
AddAutomationLaneCommand::AddAutomationLaneCommand(Arrangement& arrangement, std::size_t trackIndex, int paramIndex)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_paramIndex(paramIndex)
{
}

// Appends an empty lane for the stored parameter
void AddAutomationLaneCommand::execute() {
    m_arrangement.track(m_trackIndex).automation.push_back(AutomationLaneSlot { m_paramIndex, AutomationLane() });
}

// Removes the lane this command appended, it is always the last one
void AddAutomationLaneCommand::undo() {
    m_arrangement.track(m_trackIndex).automation.pop_back();
}

// Stores the arrangement, track, and lane index to remove on execute()
RemoveAutomationLaneCommand::RemoveAutomationLaneCommand(Arrangement& arrangement, std::size_t trackIndex,
                                                          std::size_t laneIndex)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_laneIndex(laneIndex)
{
}

// Remembers the lane's contents, then removes it
void RemoveAutomationLaneCommand::execute() {
    auto& automation = m_arrangement.track(m_trackIndex).automation;
    m_removedSlot = automation[m_laneIndex];
    automation.erase(automation.begin() + static_cast<std::ptrdiff_t>(m_laneIndex));
}

// Re-inserts the removed lane at its original index
void RemoveAutomationLaneCommand::undo() {
    auto& automation = m_arrangement.track(m_trackIndex).automation;
    automation.insert(automation.begin() + static_cast<std::ptrdiff_t>(m_laneIndex), m_removedSlot);
}

// Stores the arrangement, track, lane index, and point to add on execute()
AddAutomationPointCommand::AddAutomationPointCommand(Arrangement& arrangement, std::size_t trackIndex,
                                                      std::size_t laneIndex, AutomationPoint point)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_laneIndex(laneIndex)
    , m_point(point)
{
}

// Inserts the stored point into the lane
void AddAutomationPointCommand::execute() {
    m_arrangement.track(m_trackIndex).automation[m_laneIndex].lane.addPoint(m_point);
}

// Removes the point this command added, matched by exact tick and value
void AddAutomationPointCommand::undo() {
    AutomationLane& lane = m_arrangement.track(m_trackIndex).automation[m_laneIndex].lane;
    const auto& points = lane.points();
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (points[i].tick == m_point.tick && sameValue(points[i].value, m_point.value)) {
            lane.removePointAt(i);
            break;
        }
    }
}

// Stores the arrangement, track, lane index, and point index to remove on execute()
RemoveAutomationPointCommand::RemoveAutomationPointCommand(Arrangement& arrangement, std::size_t trackIndex,
                                                            std::size_t laneIndex, std::size_t pointIndex)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_laneIndex(laneIndex)
    , m_pointIndex(pointIndex)
{
}

// Remembers the point's data, then removes it
void RemoveAutomationPointCommand::execute() {
    AutomationLane& lane = m_arrangement.track(m_trackIndex).automation[m_laneIndex].lane;
    m_removedPoint = lane.points()[m_pointIndex];
    lane.removePointAt(m_pointIndex);
}

// Re-adds the removed point
void RemoveAutomationPointCommand::undo() {
    m_arrangement.track(m_trackIndex).automation[m_laneIndex].lane.addPoint(m_removedPoint);
}

// Stores the arrangement, track, lane index, and both endpoints of the move
MoveAutomationPointCommand::MoveAutomationPointCommand(Arrangement& arrangement, std::size_t trackIndex,
                                                        std::size_t laneIndex, AutomationPoint oldPoint,
                                                        AutomationPoint newPoint)
    : m_arrangement(arrangement)
    , m_trackIndex(trackIndex)
    , m_laneIndex(laneIndex)
    , m_oldPoint(oldPoint)
    , m_newPoint(newPoint)
{
}

// Removes the old point by exact match, adds the new one
void MoveAutomationPointCommand::execute() {
    AutomationLane& lane = m_arrangement.track(m_trackIndex).automation[m_laneIndex].lane;
    const auto& points = lane.points();
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (points[i].tick == m_oldPoint.tick && sameValue(points[i].value, m_oldPoint.value)) {
            lane.removePointAt(i);
            break;
        }
    }
    lane.addPoint(m_newPoint);
}

// Removes the new point by exact match, adds the old one back
void MoveAutomationPointCommand::undo() {
    AutomationLane& lane = m_arrangement.track(m_trackIndex).automation[m_laneIndex].lane;
    const auto& points = lane.points();
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (points[i].tick == m_newPoint.tick && sameValue(points[i].value, m_newPoint.value)) {
            lane.removePointAt(i);
            break;
        }
    }
    lane.addPoint(m_oldPoint);
}

// Appends a child, call before the composite is performed
void CompositeCommand::add(std::unique_ptr<Command> command) {
    m_children.push_back(std::move(command));
}

// Returns the number of children, an empty composite is a legal no-op
std::size_t CompositeCommand::size() const {
    return m_children.size();
}

// Executes every child in order
void CompositeCommand::execute() {
    for (auto& child : m_children) {
        child->execute();
    }
}

// Undoes every child in reverse order
void CompositeCommand::undo() {
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        (*it)->undo();
    }
}

} // namespace howl::model
