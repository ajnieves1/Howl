// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: opens every MIDI input device and fans events out to a note queue and a CC queue

#pragma once

#include "core/LockFreeQueue.h"
#include "core/MidiEvent.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <mutex>
#include <vector>

namespace howl::io {

// Opens every MIDI input device and fans events out to a note queue and a CC queue
class MidiInputHub : private juce::MidiInputCallback {
public:
    // Opens all available input devices and starts their callbacks
    MidiInputHub();

    // Stops and closes every device
    ~MidiInputHub() override;

    // Returns the note queue the audio thread drains
    LockFreeQueue<MidiEvent, 256>& noteQueue() noexcept;

    // Pops the next CC event, message thread only
    bool popCcEvent(MidiEvent& out);

    // Pushes a note event from the message thread through the same mutex real device
    // callbacks use, so a step preview can never race a real MIDI event into the queue
    bool pushNoteEvent(const MidiEvent& event);

private:
    // Normalizes one raw device message and pushes it to the right queue
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    std::mutex m_pushMutex;
    LockFreeQueue<MidiEvent, 256> m_noteQueue;
    LockFreeQueue<MidiEvent, 256> m_ccQueue;
    std::vector<std::unique_ptr<juce::MidiInput>> m_inputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiInputHub)
};

} // namespace howl::io
