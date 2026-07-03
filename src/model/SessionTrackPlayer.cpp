// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: plays one track's launched session slot, looping, switching at bar boundaries

#include "model/SessionTrackPlayer.h"

#include <cmath>

namespace howl::model {

// Stores references, the session outlives the player
SessionTrackPlayer::SessionTrackPlayer(engine::Transport& transport, const Session& session,
                                        std::size_t trackIndex, TrackKind kind)
    : m_transport(transport)
    , m_session(session)
    , m_trackIndex(trackIndex)
    , m_kind(kind)
{
}

// Stores the sample rate, call before process
void SessionTrackPlayer::prepare(double sampleRate) {
    m_sampleRate = sampleRate;
}

// Assigns the instrument MIDI slots render through, may be nullptr
void SessionTrackPlayer::setInstrument(engine::Instrument* instrument) {
    m_instrument = instrument;
}

// [RT] Queues the scene to switch to at the next bar boundary, -1 queues a stop
void SessionTrackPlayer::queueScene(int sceneIndex) noexcept {
    if (sceneIndex < 0) {
        m_stopQueued.store(true, std::memory_order_release);
        m_pendingScene.store(-1, std::memory_order_release);
    } else {
        m_pendingScene.store(sceneIndex, std::memory_order_release);
        m_stopQueued.store(false, std::memory_order_release);
    }
}

// Scene currently sounding, -1 when idle, readable from any thread
int SessionTrackPlayer::activeScene() const noexcept {
    return m_activeScene.load(std::memory_order_acquire);
}

// Scene waiting for the next boundary, -1 when none, readable from any thread
int SessionTrackPlayer::pendingScene() const noexcept {
    return m_pendingScene.load(std::memory_order_acquire);
}

// [RT] True when this track owned the whole previous block, arrangement render skipped
bool SessionTrackPlayer::ownsBlock() const noexcept {
    return m_activeScene.load(std::memory_order_relaxed) != -1;
}

// [RT] Sends noteOff for every held key, called on stop, switch, and transport stop
void SessionTrackPlayer::flushHeldNotes() noexcept {
    if (m_instrument == nullptr) {
        return;
    }

    for (int key = 0; key < 128; ++key) {
        if (m_heldKeys[key]) {
            m_instrument->noteOff(key);
            m_heldKeys[key] = false;
        }
    }
}

// [RT] Zeroes length frames of audio starting at bufferOffset
void SessionTrackPlayer::zeroSegment(AudioBlock& audio, int bufferOffset, int length) noexcept {
    for (int channel = 0; channel < audio.numChannels; ++channel) {
        for (int frame = bufferOffset; frame < bufferOffset + length; ++frame) {
            audio.channels[channel][frame] = 0.0f;
        }
    }
}

// [RT] Builds a view into audio starting at offset, length frames long, no allocation
AudioBlock SessionTrackPlayer::makeSubBlock(AudioBlock& audio, int offset, int length) noexcept {
    const int channelsToUse = audio.numChannels < kMaxChannels ? audio.numChannels : kMaxChannels;
    for (int channel = 0; channel < channelsToUse; ++channel) {
        m_channelPointers[channel] = audio.channels[channel] + offset;
    }
    return AudioBlock { m_channelPointers, channelsToUse, length };
}

// [RT] Fills events with every note on/off due in [loopPos, loopPos + length) of the clip,
// sorted by localOffset relative to loopPos. Notes whose end passes the loop seam get their
// noteOff clamped to the seam
int SessionTrackPlayer::collectClipEvents(const MidiClip& clip, double samplesPerTick, SampleCount loopSamples,
                                           SampleCount loopPos, int length,
                                           Event (&events)[kMaxEventsPerBlock]) const noexcept {
    const SampleCount windowEnd = loopPos + length;
    int count = 0;

    for (const Note& note : clip.notes()) {
        const auto noteStart = static_cast<SampleCount>(static_cast<double>(note.startTick) * samplesPerTick);
        auto noteEnd = static_cast<SampleCount>(
            static_cast<double>(note.startTick + note.lengthTicks) * samplesPerTick);

        if (noteEnd > loopSamples) {
            noteEnd = loopSamples;
        }

        if (noteStart >= loopPos && noteStart < windowEnd && count < kMaxEventsPerBlock) {
            events[count] = Event { static_cast<int>(noteStart - loopPos), true, note.key, note.velocity };
            ++count;
        }
        if (noteEnd >= loopPos && noteEnd < windowEnd && count < kMaxEventsPerBlock) {
            events[count] = Event { static_cast<int>(noteEnd - loopPos), false, note.key, note.velocity };
            ++count;
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

// [RT] Renders one non-wrapping pass of a looping MIDI clip into audio
void SessionTrackPlayer::renderMidiLoopSegment(AudioBlock& audio, int bufferOffset, int length, SampleCount loopPos,
                                                SampleCount loopSamples, const MidiClip& clip,
                                                double samplesPerTick) noexcept {
    if (m_instrument == nullptr) {
        zeroSegment(audio, bufferOffset, length);
        return;
    }

    Event events[kMaxEventsPerBlock];
    const int numEvents = collectClipEvents(clip, samplesPerTick, loopSamples, loopPos, length, events);

    int segmentStart = 0;
    for (int i = 0; i < numEvents; ++i) {
        const Event& event = events[i];

        if (event.localOffset > segmentStart) {
            AudioBlock segment = makeSubBlock(audio, bufferOffset + segmentStart, event.localOffset - segmentStart);
            m_instrument->render(segment);
        }

        if (event.isNoteOn) {
            m_instrument->noteOn(event.key, event.velocity);
            m_heldKeys[event.key] = true;
        } else {
            m_instrument->noteOff(event.key);
            m_heldKeys[event.key] = false;
        }

        segmentStart = event.localOffset;
    }

    if (segmentStart < length) {
        AudioBlock segment = makeSubBlock(audio, bufferOffset + segmentStart, length - segmentStart);
        m_instrument->render(segment);
    }
}

// [RT] Renders one non-wrapping pass of a looping audio clip into audio
void SessionTrackPlayer::renderAudioLoopSegment(AudioBlock& audio, int bufferOffset, int length, SampleCount loopPos,
                                                 const AudioClip& clip) noexcept {
    const SampleCount clipLength = clip.lengthSamples();
    if (clipLength <= 0) {
        zeroSegment(audio, bufferOffset, length);
        return;
    }

    const int channelsToUse = audio.numChannels < clip.numChannels() ? audio.numChannels : clip.numChannels();

    for (int channel = 0; channel < channelsToUse; ++channel) {
        const float* source = clip.channelData(channel);
        for (int frame = 0; frame < length; ++frame) {
            audio.channels[channel][bufferOffset + frame] = source[loopPos + frame];
        }
    }

    for (int channel = channelsToUse; channel < audio.numChannels; ++channel) {
        for (int frame = 0; frame < length; ++frame) {
            audio.channels[channel][bufferOffset + frame] = 0.0f;
        }
    }
}

// [RT] Renders length frames of sceneIndex's clip starting at the given absolute timeline
// sample, looping and splitting across the loop seam when needed
void SessionTrackPlayer::renderActiveSegment(AudioBlock& audio, int bufferOffset, int length, SampleCount absoluteStart,
                                              int sceneIndex) noexcept {
    if (length <= 0) {
        return;
    }

    if (sceneIndex < 0 || m_trackIndex >= m_session.numTracks()
        || static_cast<std::size_t>(sceneIndex) >= m_session.numScenes()) {
        zeroSegment(audio, bufferOffset, length);
        return;
    }

    const ClipSlot& clipSlot = m_session.slot(m_trackIndex, static_cast<std::size_t>(sceneIndex));

    if (m_kind == TrackKind::Midi) {
        if (clipSlot.content != SlotContent::Midi) {
            zeroSegment(audio, bufferOffset, length);
            return;
        }

        const MidiClip& clip = clipSlot.midiClip;
        const double tempo = m_transport.tempo();
        const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(kTicksPerQuarter);
        const auto loopSamples = static_cast<SampleCount>(
            std::llround(static_cast<double>(clip.lengthTicks()) * samplesPerTick));
        const SampleCount clampedLoopSamples = loopSamples > 0 ? loopSamples : 1;

        SampleCount loopPos = (absoluteStart - m_activationSample) % clampedLoopSamples;
        if (loopPos < 0) {
            loopPos += clampedLoopSamples;
        }

        if (loopPos + length <= clampedLoopSamples) {
            renderMidiLoopSegment(audio, bufferOffset, length, loopPos, clampedLoopSamples, clip, samplesPerTick);
        } else {
            const auto firstLength = static_cast<int>(clampedLoopSamples - loopPos);
            renderMidiLoopSegment(audio, bufferOffset, firstLength, loopPos, clampedLoopSamples, clip, samplesPerTick);
            renderMidiLoopSegment(audio, bufferOffset + firstLength, length - firstLength, 0,
                                   clampedLoopSamples, clip, samplesPerTick);
        }
    } else {
        if (clipSlot.content != SlotContent::Audio) {
            zeroSegment(audio, bufferOffset, length);
            return;
        }

        const AudioClip& clip = clipSlot.audioClip;
        const SampleCount loopSamples = clip.lengthSamples();

        if (loopSamples <= 0) {
            zeroSegment(audio, bufferOffset, length);
            return;
        }

        SampleCount loopPos = (absoluteStart - m_activationSample) % loopSamples;
        if (loopPos < 0) {
            loopPos += loopSamples;
        }

        if (loopPos + length <= loopSamples) {
            renderAudioLoopSegment(audio, bufferOffset, length, loopPos, clip);
        } else {
            const auto firstLength = static_cast<int>(loopSamples - loopPos);
            renderAudioLoopSegment(audio, bufferOffset, firstLength, loopPos, clip);
            renderAudioLoopSegment(audio, bufferOffset + firstLength, length - firstLength, 0, clip);
        }
    }
}

// [RT] Renders the active slot into audio from the activation offset onward,
// applying any pending switch at the bar boundary inside this block
void SessionTrackPlayer::process(AudioBlock& audio, SampleCount pos) noexcept {
    const int numFrames = audio.numFrames;
    const int activeAtStart = m_activeScene.load(std::memory_order_relaxed);
    const int pendingSceneLocal = m_pendingScene.load(std::memory_order_acquire);
    const bool stopQueuedLocal = m_stopQueued.load(std::memory_order_acquire);
    const bool hasPendingAction = stopQueuedLocal || pendingSceneLocal != -1;

    int switchOffset = numFrames;
    bool hasSwitch = false;

    if (hasPendingAction) {
        const double tempo = m_transport.tempo();
        const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(kTicksPerQuarter);
        const double barSamplesD = samplesPerTick * static_cast<double>(kTicksPerQuarter) * 4.0;

        if (barSamplesD > 0.0) {
            const double posD = static_cast<double>(pos);
            const auto boundary = static_cast<SampleCount>(std::llround(std::ceil(posD / barSamplesD) * barSamplesD));

            if (boundary >= pos && boundary < pos + numFrames) {
                switchOffset = static_cast<int>(boundary - pos);
                hasSwitch = true;
            }
        }
    }

    // Pre-switch segment: only touch the buffer if this track already owned it, otherwise the
    // arrangement already rendered real content there and it must be left alone
    if (activeAtStart != -1 && switchOffset > 0) {
        renderActiveSegment(audio, 0, switchOffset, pos, activeAtStart);
    }

    if (!hasSwitch) {
        return;
    }

    flushHeldNotes();
    m_pendingScene.store(-1, std::memory_order_release);
    m_stopQueued.store(false, std::memory_order_release);

    const int postLength = numFrames - switchOffset;

    if (stopQueuedLocal) {
        if (postLength > 0) {
            zeroSegment(audio, switchOffset, postLength);
        }
        m_activeScene.store(-1, std::memory_order_release);
    } else {
        m_activationSample = pos + switchOffset;
        m_activeScene.store(pendingSceneLocal, std::memory_order_release);

        if (postLength > 0) {
            renderActiveSegment(audio, switchOffset, postLength, pos + switchOffset, pendingSceneLocal);
        }
    }
}

} // namespace howl::model
