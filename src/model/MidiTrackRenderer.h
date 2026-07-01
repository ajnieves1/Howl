// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders one MIDI track's placed clips through an instrument, sample-accurate

#pragma once

#include "core/Types.h"
#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"

namespace howl::model {

class MidiTrackRenderer {
public:
    // Stores references to the transport and track to read placements from
    MidiTrackRenderer(engine::Transport& transport, const Track& track);

    // Sets the sample rate used to convert ticks to sample offsets, call before process()
    void prepare(double sampleRate);

    // Assigns the instrument this track renders through, may be nullptr
    void setInstrument(engine::Instrument* instrument);

    // [RT] Emits due note on/off to the instrument, then renders it into audio
    void process(AudioBlock& audio, SampleCount pos) noexcept;

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
    const Track& m_track;
    engine::Instrument* m_instrument = nullptr;
    double m_sampleRate = 44100.0;
    float* m_channelPointers[kMaxChannels] {};
};

} // namespace howl::model
