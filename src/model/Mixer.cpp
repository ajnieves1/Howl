// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: per-track channel strips summed to a master strip

#include "model/Mixer.h"

namespace howl::model {

// Sizes the mixer to numTracks track strips plus a master strip
void Mixer::prepare(std::size_t numTracks, double sampleRate, int maxBlockSize, int) {
    m_trackStrips.resize(numTracks);

    for (auto& strip : m_trackStrips) {
        strip.effects().prepare(sampleRate, maxBlockSize);
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

// [RT] Applies each track strip, sums soloed/unmuted tracks, applies master, into output
void Mixer::process(const std::vector<AudioBlock>& trackBuffers, AudioBlock& output, SampleCount) noexcept {
    for (int channel = 0; channel < output.numChannels; ++channel) {
        for (int frame = 0; frame < output.numFrames; ++frame) {
            output.channels[channel][frame] = 0.0f;
        }
    }

    bool anySoloed = false;
    for (const auto& strip : m_trackStrips) {
        if (strip.soloed()) {
            anySoloed = true;
            break;
        }
    }

    for (std::size_t i = 0; i < m_trackStrips.size() && i < trackBuffers.size(); ++i) {
        AudioBlock trackBlock = trackBuffers[i];
        m_trackStrips[i].process(trackBlock);

        if (anySoloed && !m_trackStrips[i].soloed()) {
            continue;
        }

        const int channels = trackBlock.numChannels < output.numChannels ? trackBlock.numChannels : output.numChannels;
        const int frames = trackBlock.numFrames < output.numFrames ? trackBlock.numFrames : output.numFrames;

        for (int channel = 0; channel < channels; ++channel) {
            for (int frame = 0; frame < frames; ++frame) {
                output.channels[channel][frame] += trackBlock.channels[channel][frame];
            }
        }
    }

    m_masterStrip.process(output);
}

} // namespace howl::model
