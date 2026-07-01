// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: plays a MidiClip through an Instrument in sync with the transport

#pragma once

#include "core/Types.h"
#include "engine/Instrument.h"
#include "engine/Node.h"
#include "engine/Transport.h"
#include "model/MidiClip.h"

namespace hearth::model {

class SequencerNode : public engine::Node {
public:
    // Stores references to the transport, clip, and instrument to drive
    SequencerNode(engine::Transport& transport, const MidiClip& clip, engine::Instrument& instrument);

    // Sets the sample rate used to convert ticks to sample offsets, call before process()
    void prepare(double sampleRate);

    // [RT] Emits due note on/off to the instrument, then renders it into audio
    void process(AudioBlock& audio, SampleCount pos) noexcept override;

private:
    static constexpr int kMaxEventsPerBlock = 64;
    static constexpr int kMaxChannels = 32;

    struct Event {
        int localOffset;
        bool isNoteOn;
        int key;
        float velocity;
    };

    // [RT] Fills events with every note on/off due in this block, sorted by localOffset
    int collectEvents(SampleCount pos, int numFrames, Event (&events)[kMaxEventsPerBlock]) const noexcept;

    // [RT] Builds a view into audio starting at offset, length frames long, no allocation
    AudioBlock makeSubBlock(AudioBlock& audio, int offset, int length) noexcept;

    engine::Transport& m_transport;
    const MidiClip& m_clip;
    engine::Instrument& m_instrument;
    double m_sampleRate = 44100.0;
    float* m_channelPointers[kMaxChannels] {};
};

} // namespace hearth::model
