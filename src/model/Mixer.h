// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per-track channel strips routed through buses and sends to a master strip

#pragma once

#include "core/Types.h"
#include "engine/Meter.h"
#include "model/ChannelStrip.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace howl::model {

// A level-controlled tap from a track into a bus, pre- or post-fader
struct Send {
    std::size_t busIndex;
    float level;
    bool preFader;
};

// Which strip a command or component targets
enum class StripKind {
    Track,
    Bus,
    Master
};

struct StripAddress {
    StripKind kind;
    std::size_t index; // ignored for Master
};

class Mixer {
public:
    // Routes a track's main output back to the master strip
    static constexpr std::size_t kMaster = static_cast<std::size_t>(-1);

    // Sizes the mixer to numTracks track strips plus a master strip
    void prepare(std::size_t numTracks, double sampleRate, int maxBlockSize, int numChannels);

    // Re-sizes to numTracks reusing the sample rate/block size/channel count from the last
    // full prepare() call above; for rebuilding after reset() when those are already known
    void prepare(std::size_t numTracks);

    // Clears every strip, bus, route, send, and meter back to empty; the master strip back to
    // defaults. Off the audio thread, device paused. Follow with prepare()/addBus() to rebuild
    void reset();

    // Returns the channel strip for the given track
    ChannelStrip& trackStrip(std::size_t trackIndex);

    // Returns the master channel strip
    ChannelStrip& masterStrip();

    // Inserts a default strip (and default routing/sends/meter/pdc slot) at trackIndex, off the audio thread
    void insertTrackStrip(std::size_t trackIndex);

    // Removes the strip and its routing/sends/meter/pdc slot at trackIndex, off the audio thread
    void removeTrackStrip(std::size_t trackIndex);

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

    // Resolves a strip address to its channel strip
    ChannelStrip& strip(const StripAddress& address);

    // Returns the number of buses
    std::size_t numBuses() const;

    // Returns the name a bus was created with
    const std::string& busName(std::size_t busIndex) const;

    // Returns a track's current main output destination (a bus index or kMaster)
    std::size_t trackOutput(std::size_t trackIndex) const;

    // Returns a track's sends
    const std::vector<Send>& sends(std::size_t trackIndex) const;

    // Sets one send's level directly, continuous control, not undoable
    void setSendLevel(std::size_t trackIndex, std::size_t sendIndex, float level);

    // Post-fader track meter, filled during process
    engine::Meter& trackMeter(std::size_t trackIndex);

    // Post-fader bus meter, filled during process
    engine::Meter& busMeter(std::size_t busIndex);

    // Post-fader master meter, filled during process
    engine::Meter& masterMeter();

    // While true the track's FX chain is skipped in process and counts zero in updateLatencies
    void setTrackEffectsBypassed(std::size_t trackIndex, bool bypassed);

    // Returns whether the track's FX chain is currently bypassed
    bool trackEffectsBypassed(std::size_t trackIndex) const;

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
        std::unique_ptr<engine::Meter> meter = std::make_unique<engine::Meter>();
    };

    // Allocates a bus's scratch accumulation buffer at the mixer's current size
    void allocateBusScratch(Bus& bus);

    // [RT] Runs a track's buffer through its PDC compensation ring, in place
    void applyPdcRing(std::size_t trackIndex, AudioBlock& block) noexcept;

    // Re-derives every track's pre-fader pointer array from its buffer, after an insert/erase shift
    void repointPreFaderBlocks();

    std::vector<ChannelStrip> m_trackStrips;
    std::vector<std::size_t> m_trackOutputs;
    std::vector<std::vector<Send>> m_trackSends;
    // While true (from a freeze), the track's FX chain is skipped in process()
    std::vector<char> m_fxBypassed;
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

    // Meter holds atomics so it is not movable, vectors hold pointers
    std::vector<std::unique_ptr<engine::Meter>> m_trackMeters;
    engine::Meter m_masterMeter;
};

} // namespace howl::model
