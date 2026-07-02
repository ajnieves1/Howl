// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per-track channel strips routed through buses and sends to a master strip

#include "model/Mixer.h"

namespace howl::model {

namespace {

// Zeroes every sample in a block
void zeroBlock(AudioBlock& block) noexcept {
    for (int channel = 0; channel < block.numChannels; ++channel) {
        for (int frame = 0; frame < block.numFrames; ++frame) {
            block.channels[channel][frame] = 0.0f;
        }
    }
}

// Copies source into destination, clamped to the smaller of the two shapes
void copyBlock(const AudioBlock& source, AudioBlock& destination) noexcept {
    const int channels = source.numChannels < destination.numChannels ? source.numChannels : destination.numChannels;
    const int frames = source.numFrames < destination.numFrames ? source.numFrames : destination.numFrames;

    for (int channel = 0; channel < channels; ++channel) {
        for (int frame = 0; frame < frames; ++frame) {
            destination.channels[channel][frame] = source.channels[channel][frame];
        }
    }
}

// Accumulates source scaled by gain into destination, clamped to the smaller of the two shapes
void accumulateScaled(const AudioBlock& source, AudioBlock& destination, float gain) noexcept {
    const int channels = source.numChannels < destination.numChannels ? source.numChannels : destination.numChannels;
    const int frames = source.numFrames < destination.numFrames ? source.numFrames : destination.numFrames;

    for (int channel = 0; channel < channels; ++channel) {
        for (int frame = 0; frame < frames; ++frame) {
            destination.channels[channel][frame] += source.channels[channel][frame] * gain;
        }
    }
}

} // namespace

// Sizes the mixer to numTracks track strips plus a master strip
void Mixer::prepare(std::size_t numTracks, double sampleRate, int maxBlockSize, int numChannels) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_numChannels = numChannels;

    m_trackStrips.resize(numTracks);
    m_trackOutputs.assign(numTracks, kMaster);
    m_trackSends.assign(numTracks, std::vector<Send>());

    m_preFaderBuffers.assign(numTracks, std::vector<std::vector<float>>(
        static_cast<std::size_t>(numChannels), std::vector<float>(static_cast<std::size_t>(maxBlockSize), 0.0f)));
    m_preFaderPointers.assign(numTracks, std::vector<float*>(static_cast<std::size_t>(numChannels)));
    m_preFaderBlocks.assign(numTracks, AudioBlock { nullptr, numChannels, maxBlockSize });

    for (std::size_t track = 0; track < numTracks; ++track) {
        for (std::size_t channel = 0; channel < m_preFaderPointers[track].size(); ++channel) {
            m_preFaderPointers[track][channel] = m_preFaderBuffers[track][channel].data();
        }

        m_preFaderBlocks[track].channels = m_preFaderPointers[track].data();
    }

    for (auto& strip : m_trackStrips) {
        strip.effects().prepare(sampleRate, maxBlockSize);
    }

    for (auto& bus : m_buses) {
        bus.strip.effects().prepare(sampleRate, maxBlockSize);
        allocateBusScratch(bus);
    }

    m_masterStrip.effects().prepare(sampleRate, maxBlockSize);
}

// Returns the channel strip for the given track
ChannelStrip& Mixer::trackStrip(std::size_t trackIndex) {
    return m_trackStrips[trackIndex];
}

// Returns the master channel strip
ChannelStrip& Mixer::masterStrip() {
    return m_masterStrip;
}

// Allocates a bus's scratch accumulation buffer at the mixer's current size
void Mixer::allocateBusScratch(Bus& bus) {
    bus.channelBuffers.assign(static_cast<std::size_t>(m_numChannels),
        std::vector<float>(static_cast<std::size_t>(m_maxBlockSize), 0.0f));
    bus.channelPointers.assign(static_cast<std::size_t>(m_numChannels), nullptr);

    for (std::size_t channel = 0; channel < bus.channelPointers.size(); ++channel) {
        bus.channelPointers[channel] = bus.channelBuffers[channel].data();
    }

    bus.block = AudioBlock { bus.channelPointers.data(), m_numChannels, m_maxBlockSize };
}

// Adds a bus strip, returns its index
std::size_t Mixer::addBus(const std::string& name) {
    m_buses.push_back(Bus { name, ChannelStrip(), {}, {}, AudioBlock { nullptr, 0, 0 } });

    Bus& bus = m_buses.back();
    bus.strip.effects().prepare(m_sampleRate, m_maxBlockSize);
    allocateBusScratch(bus);

    return m_buses.size() - 1;
}

// Returns the channel strip for the given bus
ChannelStrip& Mixer::busStrip(std::size_t busIndex) {
    return m_buses[busIndex].strip;
}

// Routes a track's main output to a bus, or back to master with kMaster
void Mixer::setTrackOutput(std::size_t trackIndex, std::size_t busIndexOrMaster) {
    m_trackOutputs[trackIndex] = busIndexOrMaster;
}

// Adds a send from a track to a bus, off the audio thread
void Mixer::addSend(std::size_t trackIndex, const Send& send) {
    m_trackSends[trackIndex].push_back(send);
}

// Removes the send at index from a track, off the audio thread
void Mixer::removeSend(std::size_t trackIndex, std::size_t sendIndex) {
    auto& sends = m_trackSends[trackIndex];
    sends.erase(sends.begin() + static_cast<std::ptrdiff_t>(sendIndex));
}

// [RT] Runs track strips, routes main outputs and sends into buses/master, sums buses, applies master
void Mixer::process(const std::vector<AudioBlock>& trackBuffers, AudioBlock& output, SampleCount) noexcept {
    zeroBlock(output);

    for (auto& bus : m_buses) {
        zeroBlock(bus.block);
    }

    bool anySoloed = false;
    for (const auto& strip : m_trackStrips) {
        if (strip.soloed()) {
            anySoloed = true;
            break;
        }
    }

    for (std::size_t i = 0; i < m_trackStrips.size() && i < trackBuffers.size(); ++i) {
        ChannelStrip& strip = m_trackStrips[i];

        if (strip.muted()) {
            continue;
        }

        if (anySoloed && !strip.soloed()) {
            continue;
        }

        AudioBlock trackBlock = trackBuffers[i];
        strip.processEffects(trackBlock);

        AudioBlock& preFaderBlock = m_preFaderBlocks[i];
        copyBlock(trackBlock, preFaderBlock);

        strip.applyGain(trackBlock);

        for (const Send& send : m_trackSends[i]) {
            if (send.busIndex >= m_buses.size()) {
                continue;
            }

            const AudioBlock& source = send.preFader ? preFaderBlock : trackBlock;
            accumulateScaled(source, m_buses[send.busIndex].block, send.level);
        }

        const std::size_t destination = m_trackOutputs[i];

        if (destination == kMaster) {
            accumulateScaled(trackBlock, output, 1.0f);
        } else if (destination < m_buses.size()) {
            accumulateScaled(trackBlock, m_buses[destination].block, 1.0f);
        }
    }

    for (auto& bus : m_buses) {
        bus.strip.process(bus.block);
        accumulateScaled(bus.block, output, 1.0f);
    }

    m_masterStrip.process(output);
}

} // namespace howl::model
