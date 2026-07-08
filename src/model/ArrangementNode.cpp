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
    m_sampleRate = sampleRate;
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
    m_sessionPlayers.clear();
    // A full rebuild invalidates any frozen render, the mixer's bypass flags reset alongside
    m_frozenChannels.assign(numTracks, std::vector<std::vector<float>> {});
    m_trackInstruments.assign(numTracks, nullptr);

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

        if (m_session != nullptr) {
            auto player = std::make_unique<SessionTrackPlayer>(m_transport, *m_session, i, track.kind);
            player->prepare(sampleRate);
            m_sessionPlayers.push_back(std::move(player));
        }
    }

    m_mixer.prepare(numTracks, sampleRate, maxBlockSize, numChannels);
}

// Assigns the instrument a given MIDI track renders through, no-op for non-MIDI tracks
void ArrangementNode::setInstrumentForTrack(std::size_t trackIndex, engine::Instrument* instrument) {
    if (trackIndex < m_midiRenderers.size() && m_midiRenderers[trackIndex] != nullptr) {
        m_midiRenderers[trackIndex]->setInstrument(instrument);
    }
    if (trackIndex < m_sessionPlayers.size() && m_sessionPlayers[trackIndex] != nullptr) {
        m_sessionPlayers[trackIndex]->setInstrument(instrument);
    }
    if (trackIndex < m_trackInstruments.size()) {
        m_trackInstruments[trackIndex] = instrument;
    }
}

// Returns the mixer driving every track's gain, pan, mute, solo, and effects
Mixer& ArrangementNode::mixer() {
    return m_mixer;
}

// Points session playback at the grid, off the audio thread, set before prepare
void ArrangementNode::setSession(const Session* session) {
    m_session = session;
}

// Clears active and pending session playback so a render starts from pure arrangement state
void ArrangementNode::resetSessionPlayback() {
    LaunchRequest discard;
    while (m_launchQueue.pop(discard)) {
        // drop every queued launch, nothing should apply once the render starts
    }

    if (m_session == nullptr) {
        return;
    }

    for (std::size_t i = 0; i < m_sessionPlayers.size(); ++i) {
        if (m_sessionPlayers[i] == nullptr) {
            continue;
        }

        auto fresh = std::make_unique<SessionTrackPlayer>(m_transport, *m_session, i, m_arrangement.track(i).kind);
        fresh->prepare(m_sampleRate);
        m_sessionPlayers[i] = std::move(fresh);
    }
}

// Queues a quantized launch from the UI thread, false when the queue is full
bool ArrangementNode::requestLaunch(std::size_t trackIndex, int sceneIndex) {
    return m_launchQueue.push(LaunchRequest { trackIndex, sceneIndex });
}

// Queues a quantized stop from the UI thread, false when the queue is full
bool ArrangementNode::requestStop(std::size_t trackIndex) {
    return m_launchQueue.push(LaunchRequest { trackIndex, -1 });
}

// Scene a track is playing, -1 when it follows the arrangement, UI polling
int ArrangementNode::activeScene(std::size_t trackIndex) const noexcept {
    if (trackIndex < m_sessionPlayers.size() && m_sessionPlayers[trackIndex] != nullptr) {
        return m_sessionPlayers[trackIndex]->activeScene();
    }
    return -1;
}

// Scene a track has queued, -1 when none, UI polling
int ArrangementNode::pendingScene(std::size_t trackIndex) const noexcept {
    if (trackIndex < m_sessionPlayers.size() && m_sessionPlayers[trackIndex] != nullptr) {
        return m_sessionPlayers[trackIndex]->pendingScene();
    }
    return -1;
}

// Installs a frozen render, playback reads it and skips live rendering, off thread, device paused
void ArrangementNode::setFrozen(std::size_t trackIndex, std::vector<std::vector<float>> channels) {
    if (trackIndex >= m_frozenChannels.size()) {
        return;
    }

    m_frozenChannels[trackIndex] = std::move(channels);
    m_mixer.setTrackEffectsBypassed(trackIndex, true);
    m_mixer.updateLatencies();
}

// Drops a track's frozen render, live rendering resumes
void ArrangementNode::clearFrozen(std::size_t trackIndex) {
    if (trackIndex >= m_frozenChannels.size()) {
        return;
    }

    m_frozenChannels[trackIndex].clear();
    m_mixer.setTrackEffectsBypassed(trackIndex, false);
    m_mixer.updateLatencies();
}

// True when the track plays a frozen render
bool ArrangementNode::isFrozen(std::size_t trackIndex) const {
    return trackIndex < m_frozenChannels.size() && !m_frozenChannels[trackIndex].empty();
}

// Points the node at the queue of live note events to drain each block, call before prepare
void ArrangementNode::setLiveNoteQueue(LockFreeQueue<MidiEvent, 256>* queue) {
    m_liveNoteQueue = queue;
}

// Selects which track live notes play into, -1 for none, callable from any thread
void ArrangementNode::setLiveTargetTrack(std::ptrdiff_t trackIndex) {
    m_liveTargetTrack.store(trackIndex, std::memory_order_relaxed);
}

// Points the node at the app's preview player, call before prepare
void ArrangementNode::setPreviewPlayer(PreviewPlayer* player) {
    m_previewPlayer = player;
}

// [RT] Renders every track into its own buffer, then mixes them into audio
void ArrangementNode::process(AudioBlock& audio, SampleCount pos) noexcept {
    // Guards against a block larger than what prepare() sized the scratch
    // buffers for, any frames beyond this stay silent rather than overrun them
    const int frames = audio.numFrames > m_maxFrames ? m_maxFrames : audio.numFrames;
    const int channels = m_numChannels < audio.numChannels ? m_numChannels : audio.numChannels;

    LaunchRequest request;
    while (m_launchQueue.pop(request)) {
        const bool requestTargetFrozen = request.trackIndex < m_frozenChannels.size()
            && !m_frozenChannels[request.trackIndex].empty();

        if (!requestTargetFrozen && request.trackIndex < m_sessionPlayers.size()
            && m_sessionPlayers[request.trackIndex] != nullptr) {
            m_sessionPlayers[request.trackIndex]->queueScene(request.sceneIndex);
        }
    }

    // Drains every block whether the transport is playing or stopped, so a keyboard is always
    // live; a target that resolves to nothing still gets its events discarded here rather than
    // building up and bursting out once a valid target is picked later
    MidiEvent liveEvent;
    while (m_liveNoteQueue != nullptr && m_liveNoteQueue->pop(liveEvent)) {
        const std::ptrdiff_t target = m_liveTargetTrack.load(std::memory_order_relaxed);
        if (target < 0 || static_cast<std::size_t>(target) >= m_trackInstruments.size()) {
            continue;
        }

        const auto targetTrack = static_cast<std::size_t>(target);
        if (targetTrack < m_frozenChannels.size() && !m_frozenChannels[targetTrack].empty()) {
            continue;
        }

        engine::Instrument* instrument = m_trackInstruments[targetTrack];
        if (instrument == nullptr) {
            continue;
        }

        if (liveEvent.type == MidiEvent::Type::NoteOn) {
            instrument->noteOn(liveEvent.number, liveEvent.value);
        } else if (liveEvent.type == MidiEvent::Type::NoteOff) {
            instrument->noteOff(liveEvent.number);
        }
    }

    for (std::size_t i = 0; i < m_trackBlocks.size(); ++i) {
        AudioBlock& trackBlock = m_trackBlocks[i];
        trackBlock.numChannels = channels;
        trackBlock.numFrames = frames;

        if (i < m_frozenChannels.size() && !m_frozenChannels[i].empty()) {
            const auto& frozenBuffer = m_frozenChannels[i];
            for (int channel = 0; channel < trackBlock.numChannels; ++channel) {
                const auto& source = frozenBuffer[static_cast<std::size_t>(channel)];
                for (int frame = 0; frame < trackBlock.numFrames; ++frame) {
                    const auto sampleIndex = static_cast<std::size_t>(pos + frame);
                    trackBlock.channels[channel][frame] = sampleIndex < source.size() ? source[sampleIndex] : 0.0f;
                }
            }
            continue;
        }

        SessionTrackPlayer* player = i < m_sessionPlayers.size() ? m_sessionPlayers[i].get() : nullptr;
        const bool playerOwnsBlock = player != nullptr && player->ownsBlock();

        if (!playerOwnsBlock) {
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

        if (player != nullptr) {
            player->process(trackBlock, pos);
        }
    }

    m_mixer.process(m_trackBlocks, audio, pos);

    if (m_previewPlayer != nullptr) {
        m_previewPlayer->process(audio);
    }
}

} // namespace howl::model
