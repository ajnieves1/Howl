// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: one incoming MIDI event normalized for queueing between threads

#pragma once

namespace howl {

// One incoming MIDI event normalized for queueing
struct MidiEvent {
    enum class Type { NoteOn, NoteOff, ControlChange };

    // What kind of event this is
    Type type;

    // Key number for notes, controller number for CC
    int number;

    // Velocity or controller value, normalized 0..1
    float value;
};

} // namespace howl
