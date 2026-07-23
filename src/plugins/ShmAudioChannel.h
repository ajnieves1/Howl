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
    // Set once by the child right before it starts waiting for input, so the parent
    // knows the first exchange will actually be heard
    std::atomic<std::uint32_t> childReady;
    // Geometry both sides agreed on at setup
    std::int32_t numChannels;
    std::int32_t blockSize;
    // Frames the parent actually wrote for this one exchange, never more than blockSize. The
    // renderer splits a block at every note event, so a sub block is normal and the child must
    // render exactly this many frames or the plugin's own timeline runs ahead
    std::int32_t numFrames;
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

    // Floor for the exchange deadline. The real deadline is set from the block period by
    // setExchangeTimeoutMicros(), since a fixed 2 ms cuts a plugin off long before its actual
    // real time budget and makes a larger buffer size do nothing for glitching
    static constexpr int kExchangeTimeoutMicros = 2000;

    // Sets the deadline exchange() waits for the child, clamped to a sane range. Call from the
    // message thread before streaming, it is read on the audio thread
    void setExchangeTimeoutMicros(int micros) noexcept;

    // Child side: waitForInput() busy spins with no sleep at all up to this long before
    // it falls back to sleeping between checks. Windows rounds a sub millisecond
    // sleep_for up to its default timer resolution, commonly around 15 ms without a
    // process wide timeBeginPeriod call, which would blow the parent's exchange
    // deadline on almost every block during active streaming if the child slept from
    // the very first check
    static constexpr int kWaitForInputSpinMicros = 5000;

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

    // Child side: marks this side ready, called immediately before the first waitForInput().
    // The parent must not exchange before this: a failed exchange still bumps the sequence,
    // and a child whose first wait starts more than one sequence behind can never catch up
    void signalReady();

    // Parent side: polls for the child's ready mark, sleeping between checks. Message
    // thread only, never the audio thread. False when timeoutMs passes without it
    bool waitForChildReady(int timeoutMs) const;

    // Parent side: polls until the child's output sequence has caught up with the input
    // sequence, sleeping between checks. After a deadline-missed exchange this is the safe
    // way to wait before exchanging again: the input sequence holds its value between
    // exchanges, so a caught-up child can never be left waiting for a value that already
    // passed. Message thread only. False when timeoutMs passes first
    bool waitForOutputToCatchUp(int timeoutMs) const;

    // Child side: direct views into the mapped input/output/event regions
    float* const* inputChannels();
    float* const* outputChannels();

    // Child side: frames the parent wrote for the exchange just received, clamped to blockSize
    int numFrames() const;
    const MidiEvent* events(int& numEvents) const;

private:
    ShmAudioChannel() = default;

    // Resolves every region pointer from an already-opened mapping; isCreator also
    // placement-news the header and zeros the sequence counters, a non-creator trusts the
    // geometry already there. Takes ownership of a mapping the caller already opened,
    // rather than opening its own second one: on Windows, a still-open read-only handle
    // does not grant FILE_SHARE_WRITE, so a second, separate read-write open of the same
    // file deterministically fails with a sharing violation while the first stays open
    static std::unique_ptr<ShmAudioChannel> fromMappedFile(std::unique_ptr<juce::MemoryMappedFile> mapped,
                                                           bool isCreator, int numChannels, int blockSize);

    std::unique_ptr<juce::MemoryMappedFile> m_mappedFile;

    ShmHeader* m_header = nullptr;
    std::int32_t* m_eventFrameOffsets = nullptr;
    MidiEvent* m_events = nullptr;

    std::vector<float*> m_inputChannelPtrs;
    std::vector<float*> m_outputChannelPtrs;

    int m_numChannels = 0;
    int m_blockSize = 0;

    // The live exchange deadline, set from the block period, read on the audio thread
    std::atomic<int> m_exchangeTimeoutMicros { kExchangeTimeoutMicros };

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
