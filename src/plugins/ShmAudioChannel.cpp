// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: one block synchronous audio exchange between a parent and a sandboxed child, over shared memory

#include "plugins/ShmAudioChannel.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#if JUCE_LINUX || JUCE_MAC
#include <unistd.h>
#endif

namespace howl::plugins {

namespace {

// Total bytes the mapped file needs for the given geometry
size_t totalMappedSize(int numChannels, int blockSize) {
    return sizeof(ShmHeader)
        + static_cast<size_t>(ShmAudioChannel::kMaxEvents) * sizeof(std::int32_t)
        + static_cast<size_t>(ShmAudioChannel::kMaxEvents) * sizeof(MidiEvent)
        + 2 * static_cast<size_t>(numChannels) * static_cast<size_t>(blockSize) * sizeof(float);
}

} // namespace

// Cleans up the mapped file via the member unique_ptr, nothing else owned
ShmAudioChannel::~ShmAudioChannel() = default;

// Creates and maps the backing file, parent side
std::unique_ptr<ShmAudioChannel> ShmAudioChannel::create(const juce::File& backingFile, int numChannels, int blockSize) {
    const size_t size = totalMappedSize(numChannels, blockSize);
    std::vector<char> zeros(size, 0);
    if (!backingFile.replaceWithData(zeros.data(), zeros.size())) {
        return nullptr;
    }

    auto mapped = std::make_unique<juce::MemoryMappedFile>(backingFile, juce::MemoryMappedFile::readWrite);
    if (mapped->getData() == nullptr || mapped->getSize() < size) {
        return nullptr;
    }

    return fromMappedFile(std::move(mapped), true, numChannels, blockSize);
}

// Maps an existing backing file, child side; reads the geometry the parent already wrote
// from the same mapping used afterward, rather than a separate probe mapping first
std::unique_ptr<ShmAudioChannel> ShmAudioChannel::open(const juce::File& backingFile) {
    auto mapped = std::make_unique<juce::MemoryMappedFile>(backingFile, juce::MemoryMappedFile::readWrite);
    if (mapped->getData() == nullptr || mapped->getSize() < sizeof(ShmHeader)) {
        return nullptr;
    }

    const auto* header = static_cast<const ShmHeader*>(mapped->getData());
    const int numChannels = header->numChannels;
    const int blockSize = header->blockSize;

    if (mapped->getSize() < totalMappedSize(numChannels, blockSize)) {
        return nullptr;
    }

    return fromMappedFile(std::move(mapped), false, numChannels, blockSize);
}

// Resolves every region pointer from an already-opened mapping; isCreator also
// placement-news the header and zeros the sequence counters, a non-creator trusts the
// geometry already there
std::unique_ptr<ShmAudioChannel> ShmAudioChannel::fromMappedFile(std::unique_ptr<juce::MemoryMappedFile> mapped,
                                                                 bool isCreator, int numChannels, int blockSize) {
    if (numChannels <= 0 || blockSize <= 0) {
        return nullptr;
    }

    auto channel = std::unique_ptr<ShmAudioChannel>(new ShmAudioChannel());
    channel->m_mappedFile = std::move(mapped);
    channel->m_numChannels = numChannels;
    channel->m_blockSize = blockSize;

    auto* base = static_cast<std::uint8_t*>(channel->m_mappedFile->getData());
    channel->m_header = reinterpret_cast<ShmHeader*>(base);

    auto* frameOffsetsBase = reinterpret_cast<std::int32_t*>(base + sizeof(ShmHeader));
    channel->m_eventFrameOffsets = frameOffsetsBase;

    auto* eventsBase = reinterpret_cast<MidiEvent*>(frameOffsetsBase + kMaxEvents);
    channel->m_events = eventsBase;

    auto* inputBase = reinterpret_cast<float*>(eventsBase + kMaxEvents);
    auto* outputBase = inputBase + static_cast<size_t>(numChannels) * static_cast<size_t>(blockSize);

    channel->m_inputChannelPtrs.resize(static_cast<size_t>(numChannels));
    channel->m_outputChannelPtrs.resize(static_cast<size_t>(numChannels));
    for (int c = 0; c < numChannels; ++c) {
        channel->m_inputChannelPtrs[static_cast<size_t>(c)] = inputBase + static_cast<size_t>(c) * static_cast<size_t>(blockSize);
        channel->m_outputChannelPtrs[static_cast<size_t>(c)] = outputBase + static_cast<size_t>(c) * static_cast<size_t>(blockSize);
    }

    if (isCreator) {
        // A non-creator must never placement-new the header, that would stomp the
        // sequence counters the other side may already be reading or writing
        new (channel->m_header) ShmHeader {};
        channel->m_header->numChannels = numChannels;
        channel->m_header->blockSize = blockSize;
        channel->m_header->numEvents = 0;
    }

   #if JUCE_LINUX || JUCE_MAC
    if (!isCreator) {
        channel->m_expectedParentPid = static_cast<long>(getppid());
    }
   #endif

    return channel;
}

// [RT] Writes input and events, bumps inputSeq, spin-waits for the output.
// Returns false and outputs silence when the child misses the deadline
bool ShmAudioChannel::exchange(AudioBlock& audio, const MidiEvent* events, int numEvents) noexcept {
    const int channelsToCopy = std::min(audio.numChannels, m_numChannels);
    const int framesToCopy = std::min(audio.numFrames, m_blockSize);

    for (int c = 0; c < channelsToCopy; ++c) {
        std::memcpy(m_inputChannelPtrs[static_cast<size_t>(c)], audio.channels[c],
                    static_cast<size_t>(framesToCopy) * sizeof(float));
    }

    const int clampedEventCount = std::min(numEvents, kMaxEvents);
    for (int i = 0; i < clampedEventCount; ++i) {
        m_eventFrameOffsets[i] = 0;
        m_events[i] = events[i];
    }
    m_header->numEvents = clampedEventCount;

    const std::uint32_t expected = m_header->inputSeq.fetch_add(1, std::memory_order_release) + 1;

    // One clock read anchors the deadline; steady_clock::now() is a vDSO read on Linux
    // with no syscall, the one exception in the codebase to reading a clock on this thread
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(kExchangeTimeoutMicros);

    bool matched = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_header->outputSeq.load(std::memory_order_acquire) == expected) {
            matched = true;
            break;
        }
    }

    if (!matched) {
        for (int c = 0; c < audio.numChannels; ++c) {
            std::memset(audio.channels[c], 0, static_cast<size_t>(audio.numFrames) * sizeof(float));
        }
        return false;
    }

    for (int c = 0; c < channelsToCopy; ++c) {
        std::memcpy(audio.channels[c], m_outputChannelPtrs[static_cast<size_t>(c)],
                    static_cast<size_t>(framesToCopy) * sizeof(float));
    }

    return true;
}

// Child side: blocks until the next input block arrives, false when the parent vanished
bool ShmAudioChannel::waitForInput() {
    const std::uint32_t target = m_lastHandledInputSeq + 1;

    while (m_header->inputSeq.load(std::memory_order_acquire) != target) {
       #if JUCE_LINUX || JUCE_MAC
        if (static_cast<long>(getppid()) != m_expectedParentPid) {
            return false;
        }
       #endif
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    m_lastHandledInputSeq = target;
    return true;
}

// Child side: publishes the processed block and bumps outputSeq and childAlive
void ShmAudioChannel::publishOutput() {
    // Stamps the sequence this side actually just handled, not whatever inputSeq reads
    // as right now, the parent may already be several blocks further ahead
    m_header->outputSeq.store(m_lastHandledInputSeq, std::memory_order_release);
    m_header->childAlive.fetch_add(1, std::memory_order_relaxed);
}

// Child side: direct view into the mapped input region
float* const* ShmAudioChannel::inputChannels() {
    return m_inputChannelPtrs.data();
}

// Child side: direct view into the mapped output region
float* const* ShmAudioChannel::outputChannels() {
    return m_outputChannelPtrs.data();
}

// Child side: direct view into the mapped event region
const MidiEvent* ShmAudioChannel::events(int& numEvents) const {
    numEvents = m_header->numEvents;
    return m_events;
}

} // namespace howl::plugins
