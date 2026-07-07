// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: one block synchronous audio exchange between a parent and a sandboxed child, over shared memory

#pragma once

#include "core/MidiEvent.h"
#include "core/Types.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace howl::plugins {

// Fixed shared memory layout at the start of the mapped file
struct ShmHeader {
    // Incremented by the parent after writing an input block
    std::atomic<std::uint32_t> inputSeq;
    // Incremented by the child after writing the matching output block
    std::atomic<std::uint32_t> outputSeq;
    // Heartbeat the child bumps every block so the parent can detect a hang
    std::atomic<std::uint32_t> childAlive;
    // Geometry both sides agreed on at setup
    std::int32_t numChannels;
    std::int32_t blockSize;
    // Events the parent wrote for this block, instrument plugins read them
    std::int32_t numEvents;
};

// One block synchronous audio exchange over a memory mapped file: the parent writes an
// input block and spin-waits for the matching output, the child blocks politely for input
// and publishes output when done. Layout: ShmHeader, then a fixed 64-slot frame-offset array,
// then a fixed 64-slot MidiEvent array (kept as two parallel arrays rather than one array of
// pairs, so events() can hand back a flat, contiguous MidiEvent pointer), then numChannels *
// blockSize input floats, then the same for output. Every event's frame offset is 0 in this
// phase, exchange() has no per-event offset to record yet, a true offset is a later refinement
class ShmAudioChannel {
public:
    static constexpr int kMaxEvents = 64;

    // The parent gives up and returns silence past this deadline, taken once per exchange()
    static constexpr int kExchangeTimeoutMicros = 2000;

    ~ShmAudioChannel();

    ShmAudioChannel(const ShmAudioChannel&) = delete;
    ShmAudioChannel& operator=(const ShmAudioChannel&) = delete;

    // Creates and maps the backing file, parent side
    static std::unique_ptr<ShmAudioChannel> create(const juce::File& backingFile, int numChannels, int blockSize);

    // Maps an existing backing file, child side; reads the geometry the parent already wrote
    static std::unique_ptr<ShmAudioChannel> open(const juce::File& backingFile);

    // [RT] Writes input and events, bumps inputSeq, spin-waits for the output.
    // Returns false and outputs silence when the child misses the deadline
    bool exchange(AudioBlock& audio, const MidiEvent* events, int numEvents) noexcept;

    // Child side: blocks until the next input block arrives, false when the parent vanished
    bool waitForInput();

    // Child side: publishes the processed block and bumps outputSeq and childAlive
    void publishOutput();

    // Child side: direct views into the mapped input/output/event regions
    float* const* inputChannels();
    float* const* outputChannels();
    const MidiEvent* events(int& numEvents) const;

private:
    ShmAudioChannel() = default;

    // Maps the file and resolves every region pointer; isCreator also placement-news the
    // header and zeros the sequence counters, a non-creator trusts the geometry already there
    static std::unique_ptr<ShmAudioChannel> mapExisting(const juce::File& backingFile, bool isCreator,
                                                        int numChannels, int blockSize);

    std::unique_ptr<juce::MemoryMappedFile> m_mappedFile;

    ShmHeader* m_header = nullptr;
    std::int32_t* m_eventFrameOffsets = nullptr;
    MidiEvent* m_events = nullptr;

    std::vector<float*> m_inputChannelPtrs;
    std::vector<float*> m_outputChannelPtrs;

    int m_numChannels = 0;
    int m_blockSize = 0;

    // The child's own record of the last block it fully handled, so publishOutput() stamps
    // the exact sequence it processed even if the parent has since moved further ahead
    std::uint32_t m_lastHandledInputSeq = 0;

   #if JUCE_LINUX || JUCE_MAC
    // Captured at open() on the child side, so waitForInput() can notice a reparent (the
    // parent process died and this process now belongs to init/launchd instead)
    long m_expectedParentPid = 0;
   #endif
};

} // namespace howl::plugins
