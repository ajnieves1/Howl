// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: plays one track's launched session slot, looping, switching at bar boundaries

#pragma once

#include "core/Types.h"
#include "engine/Instrument.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/MidiClip.h"
#include "model/Session.h"

#include <atomic>
#include <cstddef>

namespace howl::model {

class SessionTrackPlayer {
public:
    // Stores references, the session outlives the player
    SessionTrackPlayer(engine::Transport& transport, const Session& session,
                        std::size_t trackIndex, TrackKind kind);

    // Stores the sample rate, call before process
    void prepare(double sampleRate);

    // Assigns the instrument MIDI slots render through, may be nullptr
    void setInstrument(engine::Instrument* instrument);

    // [RT] Queues the scene to switch to at the next bar boundary, -1 queues a stop
    void queueScene(int sceneIndex) noexcept;

    // Scene currently sounding, -1 when idle, readable from any thread
    int activeScene() const noexcept;

    // Scene waiting for the next boundary, -1 when none, readable from any thread
    int pendingScene() const noexcept;

    // [RT] True when this track owned the whole previous block, arrangement render skipped
    bool ownsBlock() const noexcept;

    // [RT] Renders the active slot into audio from the activation offset onward,
    // applying any pending switch at the bar boundary inside this block
    void process(AudioBlock& audio, SampleCount pos) noexcept;

    // [RT] Sends noteOff for every held key, called on stop, switch, and transport stop
    void flushHeldNotes() noexcept;

private:
    static constexpr int kMaxEventsPerBlock = 64;
    static constexpr int kMaxChannels = 32;

    struct Event {
        int localOffset;
        bool isNoteOn;
        int key;
        float velocity;
    };

    // [RT] Fills events with every note on/off due in [loopPos, loopPos + length) of the clip,
    // sorted by localOffset relative to loopPos. Notes whose end passes the loop seam get their
    // noteOff clamped to the seam
    int collectClipEvents(const MidiClip& clip, double samplesPerTick, SampleCount loopSamples,
                           SampleCount loopPos, int length, Event (&events)[kMaxEventsPerBlock]) const noexcept;

    // [RT] Builds a view into audio starting at offset, length frames long, no allocation
    AudioBlock makeSubBlock(AudioBlock& audio, int offset, int length) noexcept;

    // [RT] Renders one non-wrapping pass of a looping MIDI clip into audio
    void renderMidiLoopSegment(AudioBlock& audio, int bufferOffset, int length, SampleCount loopPos,
                                SampleCount loopSamples, const MidiClip& clip, double samplesPerTick) noexcept;

    // [RT] Renders one non-wrapping pass of a looping audio clip into audio
    void renderAudioLoopSegment(AudioBlock& audio, int bufferOffset, int length, SampleCount loopPos,
                                 const AudioClip& clip) noexcept;

    // [RT] Renders length frames of sceneIndex's clip starting at the given absolute timeline
    // sample, looping and splitting across the loop seam when needed
    void renderActiveSegment(AudioBlock& audio, int bufferOffset, int length, SampleCount absoluteStart,
                              int sceneIndex) noexcept;

    // [RT] Zeroes length frames of audio starting at bufferOffset
    static void zeroSegment(AudioBlock& audio, int bufferOffset, int length) noexcept;

    engine::Transport& m_transport;
    const Session& m_session;
    std::size_t m_trackIndex;
    TrackKind m_kind;
    engine::Instrument* m_instrument = nullptr;
    double m_sampleRate = 44100.0;
    std::atomic<int> m_activeScene { -1 };
    // -1 means no scene is queued; a queued stop is tracked separately by m_stopQueued so it
    // never collides with "no pending scene" in this field
    std::atomic<int> m_pendingScene { -1 };
    std::atomic<bool> m_stopQueued { false };
    // Timeline sample the active slot started at, loop phase reference
    SampleCount m_activationSample = 0;
    // One flag per MIDI key currently held
    bool m_heldKeys[128] {};
    float* m_channelPointers[kMaxChannels] {};
};

} // namespace howl::model
