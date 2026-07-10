// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders one MIDI track's placed clips through an instrument, sample-accurate

#pragma once

#include "core/Types.h"
#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/Pattern.h"

#include <cstddef>

namespace howl::model {

class MidiTrackRenderer {
public:
    // Stores references to the transport and track to read placements from
    MidiTrackRenderer(engine::Transport& transport, const Track& track);

    // Sets the sample rate used to convert ticks to sample offsets, call before process()
    void prepare(double sampleRate);

    // Assigns the instrument this track renders through, may be nullptr
    void setInstrument(engine::Instrument* instrument);

    // Points this renderer at the bank's placements for its track, call before process
    void setPatternSource(const PatternBank* bank, std::size_t trackIndex);

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

    // [RT] Appends clip's due note on/off events into events, respecting its own length clamp
    void collectClipEvents(int64_t placementStartTick, const MidiClip& clip, SampleCount pos, SampleCount blockEnd,
                            double samplesPerTick, Event (&events)[kMaxEventsPerBlock], int& count) const noexcept;

    // [RT] Builds a view into audio starting at offset, length frames long, no allocation
    AudioBlock makeSubBlock(AudioBlock& audio, int offset, int length) noexcept;

    engine::Transport& m_transport;
    const Track& m_track;
    engine::Instrument* m_instrument = nullptr;
    double m_sampleRate = 44100.0;
    float* m_channelPointers[kMaxChannels] {};
    const PatternBank* m_patternBank = nullptr;
    std::size_t m_patternTrackIndex = 0;

    // The pos process() last saw, so a stopped transport re polling the same frozen position
    // (every real audio callback while paused) does not retrigger a note sitting exactly
    // there over and over; -1 never matches a real position, so the first call always collects
    SampleCount m_lastProcessedPos = -1;
};

} // namespace howl::model
