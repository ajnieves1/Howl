// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: sample-accurate playhead with play, stop, loop, and tempo

#include "engine/Transport.h"

namespace hearth::engine {

// Allocates the initial, disabled loop region snapshot
Transport::Transport() {
    m_loopRegion.store(new LoopRegion { 0, 0, false }, std::memory_order_relaxed);
}

// Frees the current loop region snapshot
Transport::~Transport() {
    delete m_loopRegion.load(std::memory_order_relaxed);
}

// Starts the playhead advancing
void Transport::play() {
    m_playing.store(true, std::memory_order_relaxed);
}

// Stops the playhead advancing
void Transport::stop() {
    m_playing.store(false, std::memory_order_relaxed);
}

// Sets the tempo in beats per minute
void Transport::setTempo(double bpm) {
    m_tempo.store(bpm, std::memory_order_relaxed);
}

// Reads the current tempo in beats per minute
double Transport::tempo() const noexcept {
    return m_tempo.load(std::memory_order_relaxed);
}

// Publishes a new loop region snapshot, the old one is intentionally leaked
// since advance() may still be reading it on the audio thread
void Transport::setLoop(SampleCount start, SampleCount end, bool enabled) {
    m_loopRegion.store(new LoopRegion { start, end, enabled }, std::memory_order_release);
}

// [RT] Advances the playhead by numFrames, returns the playhead at block start
SampleCount Transport::advance(int numFrames) noexcept {
    const SampleCount blockStart = m_position.load(std::memory_order_relaxed);

    if (!m_playing.load(std::memory_order_relaxed)) {
        return blockStart;
    }

    SampleCount newPosition = blockStart + numFrames;

    const LoopRegion* loop = m_loopRegion.load(std::memory_order_acquire);
    if (loop->enabled) {
        const SampleCount loopLength = loop->end - loop->start;

        if (loopLength > 0 && newPosition >= loop->end) {
            newPosition = loop->start + ((newPosition - loop->start) % loopLength);
        }
    }

    m_position.store(newPosition, std::memory_order_relaxed);
    return blockStart;
}

// Reads the current playhead position
SampleCount Transport::position() const noexcept {
    return m_position.load(std::memory_order_relaxed);
}

} // namespace hearth::engine
