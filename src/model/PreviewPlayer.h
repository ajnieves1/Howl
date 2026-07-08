// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: RT-safe one-shot sample preview player, garbage-return handoff from the message thread

#pragma once

#include "core/LockFreeQueue.h"
#include "core/Types.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace howl::model {

// A preview sample handed from the message thread to the audio thread
struct PreviewBuffer {
    // One vector of samples per channel, equal lengths
    std::vector<std::vector<float>> channels;
};

// Plays one-shot sample previews, RT-safe handoff with a garbage-return queue
class PreviewPlayer {
public:
    // Frees anything still queued or active, call with the device stopped
    ~PreviewPlayer();

    // Queues buffer to start playing on the next block, message thread only
    void post(std::unique_ptr<PreviewBuffer> buffer);

    // Requests the active preview stop at the next block, message thread only
    void stop();

    // [RT] Mixes the active preview additively into audio, adopting posted buffers
    void process(AudioBlock& audio) noexcept;

    // Deletes buffers the audio thread released, call from a message-thread timer
    void collectGarbage();

private:
    LockFreeQueue<PreviewBuffer*, 8> m_incoming;
    LockFreeQueue<PreviewBuffer*, 8> m_retired;
    PreviewBuffer* m_active = nullptr;
    std::size_t m_playPos = 0;
    std::atomic<bool> m_stopRequested { false };
};

} // namespace howl::model
