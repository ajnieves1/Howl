// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: RT-safe one-shot sample preview player, garbage-return handoff from the message thread

#include "model/PreviewPlayer.h"

namespace howl::model {

// Frees anything still queued or active, call with the device stopped
PreviewPlayer::~PreviewPlayer() {
    delete m_active;

    PreviewBuffer* buffer = nullptr;
    while (m_incoming.pop(buffer)) {
        delete buffer;
    }
    while (m_retired.pop(buffer)) {
        delete buffer;
    }
}

// Queues buffer to start playing on the next block, message thread only. A full
// queue means nine clicks landed in one block, harmless to drop the newest
void PreviewPlayer::post(std::unique_ptr<PreviewBuffer> buffer) {
    PreviewBuffer* raw = buffer.release();
    if (!m_incoming.push(raw)) {
        delete raw;
    }
}

// Requests the active preview stop at the next block, message thread only
void PreviewPlayer::stop() {
    m_stopRequested.store(true, std::memory_order_release);
}

// [RT] Adopts any posted buffer, retiring whatever was displaced, then mixes the
// active preview additively at unity gain, mono buffers feeding every output channel
void PreviewPlayer::process(AudioBlock& audio) noexcept {
    PreviewBuffer* incoming = nullptr;
    while (m_incoming.pop(incoming)) {
        // A full retired queue here leaks the displaced buffer rather than freeing it
        // on this thread, size 8 makes that unreachable given collectGarbage runs at 30 Hz
        if (m_active != nullptr) {
            m_retired.push(m_active);
        }
        m_active = incoming;
        m_playPos = 0;
    }

    if (m_stopRequested.exchange(false, std::memory_order_acq_rel) && m_active != nullptr) {
        m_retired.push(m_active);
        m_active = nullptr;
    }

    if (m_active == nullptr || m_active->channels.empty()) {
        return;
    }

    const std::size_t sourceChannels = m_active->channels.size();
    const std::size_t sourceLength = m_active->channels[0].size();
    const std::size_t remaining = m_playPos < sourceLength ? sourceLength - m_playPos : 0;
    const auto blockFrames = static_cast<std::size_t>(audio.numFrames);
    const std::size_t toMix = remaining < blockFrames ? remaining : blockFrames;

    for (int channel = 0; channel < audio.numChannels; ++channel) {
        const std::size_t sourceChannel = static_cast<std::size_t>(channel) < sourceChannels
            ? static_cast<std::size_t>(channel) : sourceChannels - 1;
        const float* source = m_active->channels[sourceChannel].data() + m_playPos;
        float* destination = audio.channels[channel];

        for (std::size_t frame = 0; frame < toMix; ++frame) {
            destination[frame] += source[frame];
        }
    }

    m_playPos += toMix;

    if (m_playPos >= sourceLength) {
        m_retired.push(m_active);
        m_active = nullptr;
    }
}

// Deletes buffers the audio thread released, call from a message-thread timer
void PreviewPlayer::collectGarbage() {
    PreviewBuffer* buffer = nullptr;
    while (m_retired.pop(buffer)) {
        delete buffer;
    }
}

} // namespace howl::model
