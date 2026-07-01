// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders every track of an Arrangement into its own buffer and mixes them into audio

#include "model/ArrangementNode.h"

namespace howl::model {

// Stores references to the transport and arrangement to render
ArrangementNode::ArrangementNode(engine::Transport& transport, Arrangement& arrangement)
    : m_transport(transport)
    , m_arrangement(arrangement)
{
}

// Sets the sample rate, builds one renderer per track, allocates per-track scratch buffers, and prepares the mixer
void ArrangementNode::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    m_maxFrames = maxBlockSize;
    m_numChannels = numChannels;

    const std::size_t numTracks = m_arrangement.numTracks();

    m_trackChannelBuffers.assign(numTracks, std::vector<std::vector<float>>(
        static_cast<std::size_t>(numChannels), std::vector<float>(static_cast<std::size_t>(maxBlockSize), 0.0f)));
    m_trackChannelPointers.assign(numTracks, std::vector<float*>(static_cast<std::size_t>(numChannels)));
    m_trackBlocks.assign(numTracks, AudioBlock { nullptr, numChannels, maxBlockSize });

    for (std::size_t track = 0; track < numTracks; ++track) {
        for (std::size_t channel = 0; channel < m_trackChannelPointers[track].size(); ++channel) {
            m_trackChannelPointers[track][channel] = m_trackChannelBuffers[track][channel].data();
        }

        m_trackBlocks[track].channels = m_trackChannelPointers[track].data();
    }

    m_midiRenderers.clear();
    m_audioRenderers.clear();

    for (std::size_t i = 0; i < numTracks; ++i) {
        Track& track = m_arrangement.track(i);

        if (track.kind == TrackKind::Midi) {
            auto renderer = std::make_unique<MidiTrackRenderer>(m_transport, track);
            renderer->prepare(sampleRate);
            m_midiRenderers.push_back(std::move(renderer));
            m_audioRenderers.push_back(nullptr);
        } else {
            auto renderer = std::make_unique<AudioTrackRenderer>(m_transport, track);
            renderer->prepare(sampleRate);
            m_audioRenderers.push_back(std::move(renderer));
            m_midiRenderers.push_back(nullptr);
        }
    }

    m_mixer.prepare(numTracks, sampleRate, maxBlockSize, numChannels);
}

// Assigns the instrument a given MIDI track renders through, no-op for non-MIDI tracks
void ArrangementNode::setInstrumentForTrack(std::size_t trackIndex, engine::Instrument* instrument) {
    if (trackIndex < m_midiRenderers.size() && m_midiRenderers[trackIndex] != nullptr) {
        m_midiRenderers[trackIndex]->setInstrument(instrument);
    }
}

// Returns the mixer driving every track's gain, pan, mute, solo, and effects
Mixer& ArrangementNode::mixer() {
    return m_mixer;
}

// [RT] Renders every track into its own buffer, then mixes them into audio
void ArrangementNode::process(AudioBlock& audio, SampleCount pos) noexcept {
    // Guards against a block larger than what prepare() sized the scratch
    // buffers for, any frames beyond this stay silent rather than overrun them
    const int frames = audio.numFrames > m_maxFrames ? m_maxFrames : audio.numFrames;
    const int channels = m_numChannels < audio.numChannels ? m_numChannels : audio.numChannels;

    for (std::size_t i = 0; i < m_trackBlocks.size(); ++i) {
        AudioBlock& trackBlock = m_trackBlocks[i];
        trackBlock.numChannels = channels;
        trackBlock.numFrames = frames;

        if (i < m_midiRenderers.size() && m_midiRenderers[i] != nullptr) {
            m_midiRenderers[i]->process(trackBlock, pos);
        } else if (i < m_audioRenderers.size() && m_audioRenderers[i] != nullptr) {
            m_audioRenderers[i]->process(trackBlock, pos);
        } else {
            for (int channel = 0; channel < trackBlock.numChannels; ++channel) {
                for (int frame = 0; frame < trackBlock.numFrames; ++frame) {
                    trackBlock.channels[channel][frame] = 0.0f;
                }
            }
        }
    }

    m_mixer.process(m_trackBlocks, audio, pos);
}

} // namespace howl::model
