// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: opens every MIDI input device and fans events out to a note queue and a CC queue

#include "io/MidiInputHub.h"

namespace howl::io {

// Opens all available input devices and starts their callbacks
MidiInputHub::MidiInputHub() {
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        if (auto input = juce::MidiInput::openDevice(device.identifier, this)) {
            input->start();
            m_inputs.push_back(std::move(input));
        }
    }
}

// Stops and closes every device
MidiInputHub::~MidiInputHub() {
    for (auto& input : m_inputs) {
        input->stop();
    }
}

// Returns the note queue the audio thread drains
LockFreeQueue<MidiEvent, 256>& MidiInputHub::noteQueue() noexcept {
    return m_noteQueue;
}

// Pops the next CC event, message thread only
bool MidiInputHub::popCcEvent(MidiEvent& out) {
    return m_ccQueue.pop(out);
}

// Pushes a note event from the message thread through the same mutex real device
// callbacks use, so a step preview can never race a real MIDI event into the queue
bool MidiInputHub::pushNoteEvent(const MidiEvent& event) {
    const std::lock_guard<std::mutex> lock(m_pushMutex);
    return m_noteQueue.push(event);
}

// Normalizes one raw device message and pushes it to the right queue
void MidiInputHub::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    const std::lock_guard<std::mutex> lock(m_pushMutex);

    // isNoteOff() defaults to also catching a note-on sent with velocity 0,
    // the standard MIDI trick some keyboards use instead of a real note-off
    if (message.isNoteOn()) {
        m_noteQueue.push(MidiEvent { MidiEvent::Type::NoteOn, message.getNoteNumber(), message.getFloatVelocity() });
    } else if (message.isNoteOff()) {
        m_noteQueue.push(MidiEvent { MidiEvent::Type::NoteOff, message.getNoteNumber(), 0.0f });
    } else if (message.isController()) {
        m_ccQueue.push(MidiEvent { MidiEvent::Type::ControlChange, message.getControllerNumber(),
            static_cast<float>(message.getControllerValue()) / 127.0f });
    }
}

} // namespace howl::io
