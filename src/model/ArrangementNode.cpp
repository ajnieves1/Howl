// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders every track of an Arrangement into a scratch buffer and sums it into audio

#include "model/ArrangementNode.h"

namespace howl::model {

// Stores references to the transport and arrangement to render
ArrangementNode::ArrangementNode(engine::Transport& transport, Arrangement& arrangement)
    : m_transport(transport)
    , m_arrangement(arrangement)
{
}

// Sets the sample rate, builds one renderer per track, and allocates the scratch buffer
void ArrangementNode::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    m_maxFrames = maxBlockSize;

    m_scratchBuffers.assign(static_cast<std::size_t>(numChannels),
                             std::vector<float>(static_cast<std::size_t>(maxBlockSize), 0.0f));
    m_scratchPointers.resize(static_cast<std::size_t>(numChannels));
    for (std::size_t i = 0; i < m_scratchPointers.size(); ++i) {
        m_scratchPointers[i] = m_scratchBuffers[i].data();
    }

    m_midiRenderers.clear();
    m_audioRenderers.clear();

    for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
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
}

// Assigns the instrument a given MIDI track renders through, no-op for non-MIDI tracks
void ArrangementNode::setInstrumentForTrack(std::size_t trackIndex, engine::Instrument* instrument) {
    if (trackIndex < m_midiRenderers.size() && m_midiRenderers[trackIndex] != nullptr) {
        m_midiRenderers[trackIndex]->setInstrument(instrument);
    }
}

// [RT] Renders every track into scratch and sums into audio
void ArrangementNode::process(AudioBlock& audio, SampleCount pos) noexcept {
    for (int channel = 0; channel < audio.numChannels; ++channel) {
        for (int frame = 0; frame < audio.numFrames; ++frame) {
            audio.channels[channel][frame] = 0.0f;
        }
    }

    // Guards against a block larger than what prepare() sized the scratch
    // buffer for, any frames beyond this stay silent rather than overrun it
    const int frames = audio.numFrames > m_maxFrames ? m_maxFrames : audio.numFrames;
    const int scratchChannels = static_cast<int>(m_scratchPointers.size()) < audio.numChannels
        ? static_cast<int>(m_scratchPointers.size()) : audio.numChannels;

    AudioBlock scratch { m_scratchPointers.data(), scratchChannels, frames };

    for (std::size_t i = 0; i < m_midiRenderers.size(); ++i) {
        if (m_midiRenderers[i] != nullptr) {
            m_midiRenderers[i]->process(scratch, pos);
        } else if (m_audioRenderers[i] != nullptr) {
            m_audioRenderers[i]->process(scratch, pos);
        } else {
            continue;
        }

        for (int channel = 0; channel < scratchChannels; ++channel) {
            for (int frame = 0; frame < frames; ++frame) {
                audio.channels[channel][frame] += scratch.channels[channel][frame];
            }
        }
    }
}

} // namespace howl::model
