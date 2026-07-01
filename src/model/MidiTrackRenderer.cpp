// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders one MIDI track's placed clips through an instrument, sample-accurate

#include "model/MidiTrackRenderer.h"

namespace howl::model {

// Stores references to the transport and track to read placements from
MidiTrackRenderer::MidiTrackRenderer(engine::Transport& transport, const Track& track)
    : m_transport(transport)
    , m_track(track)
{
}

// Sets the sample rate used to convert ticks to sample offsets, call before process()
void MidiTrackRenderer::prepare(double sampleRate) {
    m_sampleRate = sampleRate;
}

// Assigns the instrument this track renders through, may be nullptr
void MidiTrackRenderer::setInstrument(engine::Instrument* instrument) {
    m_instrument = instrument;
}

// [RT] Fills events with every note on/off due in this block, sorted by localOffset
int MidiTrackRenderer::collectEvents(SampleCount pos, int numFrames, Event (&events)[kMaxEventsPerBlock]) const noexcept {
    const double tempo = m_transport.tempo();
    const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(kTicksPerQuarter);
    const SampleCount blockEnd = pos + numFrames;

    int count = 0;
    for (const auto& placement : m_track.midiClips) {
        for (const Note& note : placement.clip.notes()) {
            const int64_t absoluteStartTick = placement.startTick + note.startTick;
            const int64_t absoluteEndTick = absoluteStartTick + note.lengthTicks;

            const auto noteStart = static_cast<SampleCount>(static_cast<double>(absoluteStartTick) * samplesPerTick);
            const auto noteEnd = static_cast<SampleCount>(static_cast<double>(absoluteEndTick) * samplesPerTick);

            if (noteStart >= pos && noteStart < blockEnd && count < kMaxEventsPerBlock) {
                events[count] = Event { static_cast<int>(noteStart - pos), true, note.key, note.velocity };
                ++count;
            }
            if (noteEnd >= pos && noteEnd < blockEnd && count < kMaxEventsPerBlock) {
                events[count] = Event { static_cast<int>(noteEnd - pos), false, note.key, note.velocity };
                ++count;
            }
        }
    }

    // Simple insertion sort by localOffset, count is small and bounded
    for (int i = 1; i < count; ++i) {
        const Event key = events[i];
        int j = i - 1;
        while (j >= 0 && events[j].localOffset > key.localOffset) {
            events[j + 1] = events[j];
            --j;
        }
        events[j + 1] = key;
    }

    return count;
}

// [RT] Builds a view into audio starting at offset, length frames long, no allocation
AudioBlock MidiTrackRenderer::makeSubBlock(AudioBlock& audio, int offset, int length) noexcept {
    const int channelsToUse = audio.numChannels < kMaxChannels ? audio.numChannels : kMaxChannels;
    for (int channel = 0; channel < channelsToUse; ++channel) {
        m_channelPointers[channel] = audio.channels[channel] + offset;
    }
    return AudioBlock { m_channelPointers, channelsToUse, length };
}

// [RT] Emits due note on/off to the instrument, then renders it into audio
void MidiTrackRenderer::process(AudioBlock& audio, SampleCount pos) noexcept {
    if (m_instrument == nullptr) {
        for (int channel = 0; channel < audio.numChannels; ++channel) {
            for (int frame = 0; frame < audio.numFrames; ++frame) {
                audio.channels[channel][frame] = 0.0f;
            }
        }
        return;
    }

    Event events[kMaxEventsPerBlock];
    const int numEvents = collectEvents(pos, audio.numFrames, events);

    int segmentStart = 0;
    for (int i = 0; i < numEvents; ++i) {
        const Event& event = events[i];

        if (event.localOffset > segmentStart) {
            AudioBlock segment = makeSubBlock(audio, segmentStart, event.localOffset - segmentStart);
            m_instrument->render(segment);
        }

        if (event.isNoteOn) {
            m_instrument->noteOn(event.key, event.velocity);
        } else {
            m_instrument->noteOff(event.key);
        }

        segmentStart = event.localOffset;
    }

    if (segmentStart < audio.numFrames) {
        AudioBlock segment = makeSubBlock(audio, segmentStart, audio.numFrames - segmentStart);
        m_instrument->render(segment);
    }
}

} // namespace howl::model
