// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders every track of an Arrangement into its own buffer and mixes them into audio

#pragma once

#include "core/Types.h"
#include "engine/Instrument.h"
#include "engine/Node.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/AudioTrackRenderer.h"
#include "model/MidiTrackRenderer.h"
#include "model/Mixer.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace howl::model {

class ArrangementNode : public engine::Node {
public:
    // Stores references to the transport and arrangement to render
    ArrangementNode(engine::Transport& transport, Arrangement& arrangement);

    // Sets the sample rate, builds one renderer per track, allocates per-track scratch buffers, and prepares the mixer
    void prepare(double sampleRate, int maxBlockSize, int numChannels);

    // Assigns the instrument a given MIDI track renders through, no-op for non-MIDI tracks
    void setInstrumentForTrack(std::size_t trackIndex, engine::Instrument* instrument);

    // Returns the mixer driving every track's gain, pan, mute, solo, and effects
    Mixer& mixer();

    // [RT] Renders every track into its own buffer, then mixes them into audio
    void process(AudioBlock& audio, SampleCount pos) noexcept override;

private:
    engine::Transport& m_transport;
    Arrangement& m_arrangement;

    // Indexed by track, exactly one of the pair is non-null per index
    std::vector<std::unique_ptr<MidiTrackRenderer>> m_midiRenderers;
    std::vector<std::unique_ptr<AudioTrackRenderer>> m_audioRenderers;

    // Per-track scratch buffers, allocated in prepare(), never resized in process()
    std::vector<std::vector<std::vector<float>>> m_trackChannelBuffers;
    std::vector<std::vector<float*>> m_trackChannelPointers;
    std::vector<AudioBlock> m_trackBlocks;

    Mixer m_mixer;
    int m_maxFrames = 0;
    int m_numChannels = 0;
};

} // namespace howl::model
