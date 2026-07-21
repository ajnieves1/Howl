// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders every track of an Arrangement into its own buffer and mixes them into audio

#pragma once

#include "core/LockFreeQueue.h"
#include "core/MidiEvent.h"
#include "core/Types.h"
#include "engine/Instrument.h"
#include "engine/Node.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"
#include "model/AudioTrackRenderer.h"
#include "model/MidiTrackRenderer.h"
#include "model/Mixer.h"
#include "model/Pattern.h"
#include "model/PreviewPlayer.h"
#include "model/Session.h"
#include "model/SessionTrackPlayer.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace howl::model {

// UI to RT session request, sceneIndex -1 means stop the track
struct LaunchRequest {
    std::size_t trackIndex;
    int sceneIndex;
};

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

    // Points session playback at the grid, off the audio thread, set before prepare
    void setSession(const Session* session);

    // Points the node at the pattern bank, call before prepare
    void setPatternBank(const PatternBank* bank);

    // Clears active and pending session playback so a render starts from pure arrangement state
    void resetSessionPlayback();

    // Queues a quantized launch from the UI thread, false when the queue is full
    bool requestLaunch(std::size_t trackIndex, int sceneIndex);

    // Queues a quantized stop from the UI thread, false when the queue is full
    bool requestStop(std::size_t trackIndex);

    // Scene a track is playing, -1 when it follows the arrangement, UI polling
    int activeScene(std::size_t trackIndex) const noexcept;

    // Scene a track has queued, -1 when none, UI polling
    int pendingScene(std::size_t trackIndex) const noexcept;

    // Installs a frozen render, playback reads it and skips live rendering, off thread, device paused
    void setFrozen(std::size_t trackIndex, std::vector<std::vector<float>> channels);

    // Drops a track's frozen render, live rendering resumes
    void clearFrozen(std::size_t trackIndex);

    // True when the track plays a frozen render
    bool isFrozen(std::size_t trackIndex) const;

    // Points the node at the queue of live note events to drain each block, call before prepare
    void setLiveNoteQueue(LockFreeQueue<MidiEvent, 256>* queue);

    // Selects which track live notes play into, -1 for none, callable from any thread
    void setLiveTargetTrack(std::ptrdiff_t trackIndex);

    // Points the node at the app's preview player, call before prepare
    void setPreviewPlayer(PreviewPlayer* player);

    // [RT] Renders every track into its own buffer, then mixes them into audio
    void process(AudioBlock& audio, SampleCount pos) noexcept override;

private:
    engine::Transport& m_transport;
    Arrangement& m_arrangement;
    const Session* m_session = nullptr;
    const PatternBank* m_patternBank = nullptr;

    // Indexed by track, empty vector means not frozen
    std::vector<std::vector<std::vector<float>>> m_frozenChannels;

    // Mirrors the instrument pointer handed to setInstrumentForTrack, so live notes can reach
    // it directly without going through a renderer
    std::vector<engine::Instrument*> m_trackInstruments;

    LockFreeQueue<MidiEvent, 256>* m_liveNoteQueue = nullptr;
    std::atomic<std::ptrdiff_t> m_liveTargetTrack { -1 };
    PreviewPlayer* m_previewPlayer = nullptr;

    // Whether the transport was playing on the previous process block, so the play to stop
    // edge can release held notes exactly once when playback halts
    bool m_wasPlaying = false;

    // Indexed by track, exactly one of the pair is non-null per index
    std::vector<std::unique_ptr<MidiTrackRenderer>> m_midiRenderers;
    std::vector<std::unique_ptr<AudioTrackRenderer>> m_audioRenderers;

    // Indexed by track, built only when m_session is non-null
    std::vector<std::unique_ptr<SessionTrackPlayer>> m_sessionPlayers;
    LockFreeQueue<LaunchRequest, 64> m_launchQueue;

    // Per-track scratch buffers, allocated in prepare(), never resized in process()
    std::vector<std::vector<std::vector<float>>> m_trackChannelBuffers;
    std::vector<std::vector<float*>> m_trackChannelPointers;
    std::vector<AudioBlock> m_trackBlocks;

    Mixer m_mixer;
    int m_maxFrames = 0;
    int m_numChannels = 0;
    double m_sampleRate = 44100.0;
};

} // namespace howl::model
