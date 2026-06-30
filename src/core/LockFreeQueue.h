// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: single-producer single-consumer, fixed-capacity, [RT]-safe queue

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace hearth {

template <typename T, std::size_t Capacity>
class LockFreeQueue {
public:
    // [RT] Pushes item, returns false if the queue is full
    bool push(const T& item) noexcept {
        const std::size_t writeIndex = m_writeIndex.load(std::memory_order_relaxed);
        const std::size_t nextWriteIndex = increment(writeIndex);

        if (nextWriteIndex == m_readIndex.load(std::memory_order_acquire)) {
            return false;
        }

        m_buffer[writeIndex] = item;
        m_writeIndex.store(nextWriteIndex, std::memory_order_release);
        return true;
    }

    // [RT] Pops the oldest item into out, returns false if the queue is empty
    bool pop(T& out) noexcept {
        const std::size_t readIndex = m_readIndex.load(std::memory_order_relaxed);

        if (readIndex == m_writeIndex.load(std::memory_order_acquire)) {
            return false;
        }

        out = m_buffer[readIndex];
        m_readIndex.store(increment(readIndex), std::memory_order_release);
        return true;
    }

private:
    // Wraps to the next slot, one slot is always left empty so a full
    // queue and an empty queue never have the same read and write index
    static std::size_t increment(std::size_t index) noexcept {
        return (index + 1) % (Capacity + 1);
    }

    std::array<T, Capacity + 1> m_buffer {};
    std::atomic<std::size_t> m_writeIndex { 0 };
    std::atomic<std::size_t> m_readIndex { 0 };
};

} // namespace hearth
