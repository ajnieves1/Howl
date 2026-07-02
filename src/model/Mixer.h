// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per-track channel strips routed through buses and sends to a master strip

#pragma once

#include "core/Types.h"
#include "model/ChannelStrip.h"

#include <cstddef>
#include <string>
#include <vector>

namespace howl::model {

// A level-controlled tap from a track into a bus, pre- or post-fader
struct Send {
    std::size_t busIndex;
    float level;
    bool preFader;
};

class Mixer {
public:
    // Routes a track's main output back to the master strip
    static constexpr std::size_t kMaster = static_cast<std::size_t>(-1);

    // Sizes the mixer to numTracks track strips plus a master strip
    void prepare(std::size_t numTracks, double sampleRate, int maxBlockSize, int numChannels);

    // Returns the channel strip for the given track
    ChannelStrip& trackStrip(std::size_t trackIndex);

    // Returns the master channel strip
    ChannelStrip& masterStrip();

    // Adds a bus strip, returns its index
    std::size_t addBus(const std::string& name);

    // Returns the channel strip for the given bus
    ChannelStrip& busStrip(std::size_t busIndex);

    // Routes a track's main output to a bus, or back to master with kMaster
    void setTrackOutput(std::size_t trackIndex, std::size_t busIndexOrMaster);

    // Adds a send from a track to a bus, off the audio thread
    void addSend(std::size_t trackIndex, const Send& send);

    // Removes the send at index from a track, off the audio thread
    void removeSend(std::size_t trackIndex, std::size_t sendIndex);

    // Recomputes every track's compensation delay from current chain latencies, off the audio thread, transport stopped
    void updateLatencies();

    // Longest track-to-master path latency plus the master chain's own latency
    int totalLatencySamples() const noexcept;

    // [RT] Runs track strips, routes main outputs and sends into buses/master, sums buses, applies master
    void process(const std::vector<AudioBlock>& trackBuffers, AudioBlock& output, SampleCount pos) noexcept;

private:
    static constexpr int kMaxPdcSamples = 16384;

    struct Bus {
        std::string name;
        ChannelStrip strip;
        std::vector<std::vector<float>> channelBuffers;
        std::vector<float*> channelPointers;
        AudioBlock block { nullptr, 0, 0 };
    };

    // Allocates a bus's scratch accumulation buffer at the mixer's current size
    void allocateBusScratch(Bus& bus);

    // [RT] Runs a track's buffer through its PDC compensation ring, in place
    void applyPdcRing(std::size_t trackIndex, AudioBlock& block) noexcept;

    std::vector<ChannelStrip> m_trackStrips;
    std::vector<std::size_t> m_trackOutputs;
    std::vector<std::vector<Send>> m_trackSends;
    ChannelStrip m_masterStrip;

    std::vector<Bus> m_buses;

    std::vector<std::vector<std::vector<float>>> m_preFaderBuffers;
    std::vector<std::vector<float*>> m_preFaderPointers;
    std::vector<AudioBlock> m_preFaderBlocks;

    double m_sampleRate = 44100.0;
    int m_maxBlockSize = 0;
    int m_numChannels = 0;

    // One compensation ring per track per channel, sized in prepare, kMaxPdcSamples + 1 each
    std::vector<std::vector<std::vector<float>>> m_pdcLines;
    std::vector<int> m_pdcWritePos;
    std::vector<int> m_pdcSamples;
    int m_maxPathLatency = 0;
};

} // namespace howl::model
