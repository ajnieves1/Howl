// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: ClipAddress note commands against arrangement and session addressed clips

#include "model/Arrangement.h"
#include "model/Commands.h"
#include "model/Session.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using howl::model::AddNoteCommand;
using howl::model::Arrangement;
using howl::model::ClipAddress;
using howl::model::ClipSlot;
using howl::model::MidiClip;
using howl::model::MidiClipPlacement;
using howl::model::Note;
using howl::model::ReplaceNotesCommand;
using howl::model::RemoveNoteCommand;
using howl::model::Session;
using howl::model::SlotContent;
using howl::model::SplitNoteCommand;
using howl::model::TrackKind;

namespace {

// Places an empty MIDI clip on a fresh track, returns an address to it
ClipAddress makeArrangementAddress(Arrangement& arrangement) {
    const std::size_t trackIndex = arrangement.addTrack("Lead", TrackKind::Midi);
    arrangement.addMidiClipPlacement(trackIndex, MidiClipPlacement { 0, MidiClip {} });
    return ClipAddress { ClipAddress::Source::Arrangement, trackIndex, 0 };
}

// Builds a one track, one scene session with an empty MIDI slot, returns an address to it
ClipAddress makeSessionAddress(Session& session) {
    session.addTrackColumn();
    const std::size_t sceneIndex = session.addScene();
    ClipSlot& slot = session.slot(0, sceneIndex);
    slot.content = SlotContent::Midi;
    return ClipAddress { ClipAddress::Source::Session, 0, sceneIndex };
}

} // namespace

TEST_CASE("AddNoteCommand adds a note to an arrangement addressed clip and undo removes it", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);

    AddNoteCommand command(arrangement, session, nullptr, address, Note { 60, 1.0f, 0, 480 });
    command.execute();
    REQUIRE(arrangement.track(address.trackIndex).midiClips[0].clip.notes().size() == 1);

    command.undo();
    REQUIRE(arrangement.track(address.trackIndex).midiClips[0].clip.notes().empty());
}

TEST_CASE("AddNoteCommand adds a note to a session addressed clip and undo removes it", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeSessionAddress(session);

    AddNoteCommand command(arrangement, session, nullptr, address, Note { 64, 0.8f, 0, 240 });
    command.execute();
    REQUIRE(session.slot(0, address.slotIndex).midiClip.notes().size() == 1);

    command.undo();
    REQUIRE(session.slot(0, address.slotIndex).midiClip.notes().empty());
}

TEST_CASE("AddNoteCommand no-ops both ways against an unresolvable address", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address { ClipAddress::Source::Arrangement, 99, 0 };

    AddNoteCommand command(arrangement, session, nullptr, address, Note { 60, 1.0f, 0, 480 });
    command.execute();
    command.undo();
    // Nothing to assert on the model, only that neither call crashed against a bad address
}

TEST_CASE("RemoveNoteCommand removes a note from an arrangement addressed clip and undo restores it",
          "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);
    const Note note { 60, 1.0f, 0, 480 };
    arrangement.track(address.trackIndex).midiClips[0].clip.addNote(note);

    RemoveNoteCommand command(arrangement, session, nullptr, address, note);
    command.execute();
    REQUIRE(arrangement.track(address.trackIndex).midiClips[0].clip.notes().empty());

    command.undo();
    const auto& notes = arrangement.track(address.trackIndex).midiClips[0].clip.notes();
    REQUIRE(notes.size() == 1);
    REQUIRE(notes[0].key == 60);
}

TEST_CASE("RemoveNoteCommand removes a note from a session addressed clip and undo restores it", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeSessionAddress(session);
    const Note note { 67, 0.5f, 120, 240 };
    session.slot(0, address.slotIndex).midiClip.addNote(note);

    RemoveNoteCommand command(arrangement, session, nullptr, address, note);
    command.execute();
    REQUIRE(session.slot(0, address.slotIndex).midiClip.notes().empty());

    command.undo();
    REQUIRE(session.slot(0, address.slotIndex).midiClip.notes().size() == 1);
}

TEST_CASE("RemoveNoteCommand no-ops both ways when the note is not found", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);

    RemoveNoteCommand command(arrangement, session, nullptr, address, Note { 60, 1.0f, 0, 480 });
    command.execute();
    REQUIRE(arrangement.track(address.trackIndex).midiClips[0].clip.notes().empty());

    command.undo();
    REQUIRE(arrangement.track(address.trackIndex).midiClips[0].clip.notes().empty());
}

TEST_CASE("ReplaceNotesCommand moves three notes and undo restores them exactly", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);
    MidiClip& clip = arrangement.track(address.trackIndex).midiClips[0].clip;

    const std::vector<Note> before = {
        Note { 60, 1.0f, 0, 240 },
        Note { 62, 1.0f, 240, 240 },
        Note { 64, 1.0f, 480, 240 },
    };
    for (const Note& note : before) {
        clip.addNote(note);
    }

    const std::vector<Note> after = {
        Note { 61, 1.0f, 960, 240 },
        Note { 63, 1.0f, 1200, 240 },
        Note { 65, 1.0f, 1440, 240 },
    };

    ReplaceNotesCommand command(arrangement, session, nullptr, address, before, after);
    command.execute();

    REQUIRE(clip.notes().size() == 3);
    for (const Note& note : after) {
        const bool found = std::any_of(clip.notes().begin(), clip.notes().end(), [&](const Note& n) {
            return n.key == note.key && n.startTick == note.startTick;
        });
        REQUIRE(found);
    }

    command.undo();
    REQUIRE(clip.notes().size() == 3);
    for (const Note& note : before) {
        const bool found = std::any_of(clip.notes().begin(), clip.notes().end(), [&](const Note& n) {
            return n.key == note.key && n.startTick == note.startTick;
        });
        REQUIRE(found);
    }
}

TEST_CASE("ReplaceNotesCommand execute is a harmless no-op once a live drag already applied the after state",
          "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);
    MidiClip& clip = arrangement.track(address.trackIndex).midiClips[0].clip;

    const Note before { 60, 1.0f, 0, 480 };
    const Note after { 60, 1.0f, 960, 480 };
    clip.addNote(before);

    // Simulates the gesture rule: the drag already mutated the model directly before
    // the command is ever constructed or performed
    clip.replaceNoteAt(0, after);

    ReplaceNotesCommand command(arrangement, session, nullptr, address, { before }, { after });
    command.execute();

    // Still exactly one note, the already applied after, not a duplicate
    REQUIRE(clip.notes().size() == 1);
    REQUIRE(clip.notes()[0].startTick == 960);

    command.undo();
    REQUIRE(clip.notes().size() == 1);
    REQUIRE(clip.notes()[0].startTick == 0);
}

TEST_CASE("ReplaceNotesCommand with an empty before duplicates, an empty after deletes", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);
    MidiClip& clip = arrangement.track(address.trackIndex).midiClips[0].clip;

    const Note original { 60, 1.0f, 0, 480 };
    clip.addNote(original);

    const Note duplicate { 60, 1.0f, 480, 480 };
    ReplaceNotesCommand addCommand(arrangement, session, nullptr, address, {}, { duplicate });
    addCommand.execute();
    REQUIRE(clip.notes().size() == 2);

    addCommand.undo();
    REQUIRE(clip.notes().size() == 1);

    ReplaceNotesCommand deleteCommand(arrangement, session, nullptr, address, { original }, {});
    deleteCommand.execute();
    REQUIRE(clip.notes().empty());

    deleteCommand.undo();
    REQUIRE(clip.notes().size() == 1);
}

TEST_CASE("SplitNoteCommand splits a note into two halves and undo restores the original", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);
    MidiClip& clip = arrangement.track(address.trackIndex).midiClips[0].clip;

    const Note original { 60, 1.0f, 0, 480 };
    clip.addNote(original);

    SplitNoteCommand command(arrangement, session, nullptr, address, original, 200);
    command.execute();

    REQUIRE(clip.notes().size() == 2);
    REQUIRE(clip.notes()[0].startTick == 0);
    REQUIRE(clip.notes()[0].lengthTicks == 200);
    REQUIRE(clip.notes()[1].startTick == 200);
    REQUIRE(clip.notes()[1].lengthTicks == 280);

    command.undo();
    REQUIRE(clip.notes().size() == 1);
    REQUIRE(clip.notes()[0].startTick == 0);
    REQUIRE(clip.notes()[0].lengthTicks == 480);
}

TEST_CASE("SplitNoteCommand no-ops when the split point does not lie strictly inside the note", "[notecommand]") {
    Arrangement arrangement;
    Session session;
    const ClipAddress address = makeArrangementAddress(arrangement);
    MidiClip& clip = arrangement.track(address.trackIndex).midiClips[0].clip;

    const Note original { 60, 1.0f, 0, 480 };
    clip.addNote(original);

    SplitNoteCommand atStart(arrangement, session, nullptr, address, original, 0);
    atStart.execute();
    REQUIRE(clip.notes().size() == 1);

    SplitNoteCommand atEnd(arrangement, session, nullptr, address, original, 480);
    atEnd.execute();
    REQUIRE(clip.notes().size() == 1);

    SplitNoteCommand pastEnd(arrangement, session, nullptr, address, original, 900);
    pastEnd.execute();
    REQUIRE(clip.notes().size() == 1);

    atStart.undo(); // never applied, must not disturb the untouched note
    REQUIRE(clip.notes().size() == 1);
}
