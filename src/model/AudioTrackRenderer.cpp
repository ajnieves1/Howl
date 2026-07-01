// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders one audio track's placed clips, 1:1 sample copy, no time-stretch

#include "model/AudioTrackRenderer.h"

namespace howl::model {

// Stores references to the transport and track to read placements from
AudioTrackRenderer::AudioTrackRenderer(engine::Transport& transport, const Track& track)
    : m_transport(transport)
    , m_track(track)
{
}

// Sets the sample rate used to convert ticks to sample offsets, call before process()
void AudioTrackRenderer::prepare(double sampleRate) {
    m_sampleRate = sampleRate;
}

// [RT] Overwrites audio with the block's timeline range from placed clip source samples
void AudioTrackRenderer::process(AudioBlock& audio, SampleCount pos) noexcept {
    for (int channel = 0; channel < audio.numChannels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            audio.channels[channel][frame] = 0.0f;
        }
    }

    const double tempo = m_transport.tempo();
    const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(kTicksPerQuarter);
    const SampleCount blockEnd = pos + audio.numFrames;

    for (const auto& placement : m_track.audioClips) {
        const auto placementStart = static_cast<SampleCount>(static_cast<double>(placement.startTick) * samplesPerTick);
        const SampleCount clipLength = placement.clip.lengthSamples();
        const SampleCount placementEnd = placementStart + clipLength;

        if (placementEnd <= pos || placementStart >= blockEnd) {
            continue;
        }

        const SampleCount overlapStart = placementStart > pos ? placementStart : pos;
        const SampleCount overlapEnd = placementEnd < blockEnd ? placementEnd : blockEnd;

        const int channelsToUse = audio.numChannels < placement.clip.numChannels()
            ? audio.numChannels : placement.clip.numChannels();

        for (int channel = 0; channel < channelsToUse; ++channel) {
            const float* source = placement.clip.channelData(channel);
            for (SampleCount t = overlapStart; t < overlapEnd; ++t) {
                const auto destFrame = static_cast<int>(t - pos);
                const auto sourceIndex = static_cast<int>(t - placementStart);
                audio.channels[channel][destFrame] += source[sourceIndex];
            }
        }
    }
}

} // namespace howl::model
