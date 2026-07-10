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

// Points this renderer at the bank's placements for its track, call before process
void MidiTrackRenderer::setPatternSource(const PatternBank* bank, std::size_t trackIndex) {
    m_patternBank = bank;
    m_patternTrackIndex = trackIndex;
}

// [RT] Appends clip's due note on/off events into events, respecting its own length clamp
void MidiTrackRenderer::collectClipEvents(int64_t placementStartTick, const MidiClip& clip, SampleCount pos,
                                           SampleCount blockEnd, double samplesPerTick,
                                           Event (&events)[kMaxEventsPerBlock], int& count) const noexcept {
    for (const Note& note : clip.notes()) {
        // Shortening the clip silences trailing notes without discarding them, they
        // return the moment the clip is lengthened back past their start
        if (note.startTick >= clip.lengthTicks()) {
            continue;
        }

        const int64_t absoluteStartTick = placementStartTick + note.startTick;
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

// [RT] Fills events with every note on/off due in this block, sorted by localOffset
int MidiTrackRenderer::collectEvents(SampleCount pos, int numFrames, Event (&events)[kMaxEventsPerBlock]) const noexcept {
    const double tempo = m_transport.tempo();
    const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(kTicksPerQuarter);
    const SampleCount blockEnd = pos + numFrames;

    int count = 0;
    for (const auto& placement : m_track.midiClips) {
        if (placement.muted) {
            continue;
        }

        collectClipEvents(placement.startTick, placement.clip, pos, blockEnd, samplesPerTick, events, count);
    }

    if (m_patternBank != nullptr) {
        for (const auto& placement : m_patternBank->placements()) {
            if (placement.patternIndex >= m_patternBank->numPatterns()) {
                continue;
            }

            const Pattern& pattern = m_patternBank->pattern(placement.patternIndex);
            if (m_patternTrackIndex >= pattern.trackClips.size()) {
                continue;
            }

            collectClipEvents(placement.startTick, pattern.trackClips[m_patternTrackIndex], pos, blockEnd,
                               samplesPerTick, events, count);
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

    if (m_transport.isPlaying()) {
        const double tempo = m_transport.tempo();
        const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(kTicksPerQuarter);
        const auto blockStartTick = static_cast<int64_t>(static_cast<double>(pos) / samplesPerTick);

        for (const auto& slot : m_track.automation) {
            if (slot.paramIndex < 0 || slot.paramIndex >= m_instrument->numParameters()) {
                continue;
            }
            if (slot.lane.points().empty()) {
                continue;
            }
            m_instrument->setParameter(slot.paramIndex, slot.lane.valueAtTick(blockStartTick));
        }
    }

    Event events[kMaxEventsPerBlock];
    int numEvents = 0;

    // A stopped transport's advance() returns the same frozen pos on every real audio
    // callback, so a note sitting exactly there would be reported as newly due every single
    // block, retriggering it dozens of times a second instead of once. Only skip collection
    // when parked AND the position hasn't moved since the last call; offline callers like
    // TrackFreezer step pos forward themselves without ever calling play(), and always pass a
    // genuinely new pos, so they are unaffected. Playing always collects even when this
    // block's pos happens to match the last frozen one (the first block after pressing Play),
    // so landing exactly on a note still plays it once
    if (m_transport.isPlaying() || pos != m_lastProcessedPos) {
        numEvents = collectEvents(pos, audio.numFrames, events);
    }
    m_lastProcessedPos = pos;

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
