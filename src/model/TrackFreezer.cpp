// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: offline-renders one track's timeline content through its instrument and strip FX

#include "model/TrackFreezer.h"

#include "model/AudioTrackRenderer.h"
#include "model/MidiTrackRenderer.h"
#include "model/Note.h"

#include <algorithm>

namespace howl::model {

namespace {

// Returns the track's last clip end in samples, 0 if it has no clips
SampleCount lastClipEndSamples(const Track& track, double samplesPerTick) {
    SampleCount lastEnd = 0;

    for (const auto& placement : track.midiClips) {
        const int64_t endTick = placement.startTick + placement.clip.lengthTicks();
        const auto endSample = static_cast<SampleCount>(static_cast<double>(endTick) * samplesPerTick);
        lastEnd = std::max(lastEnd, endSample);
    }

    for (const auto& placement : track.audioClips) {
        const auto startSample = static_cast<SampleCount>(static_cast<double>(placement.startTick) * samplesPerTick);
        const SampleCount endSample = startSample + placement.clip.activeLengthSamples();
        lastEnd = std::max(lastEnd, endSample);
    }

    return lastEnd;
}

} // namespace

// Renders track trackIndex from sample 0 to its last clip end plus one second of tail,
// returns per-channel buffers, empty when the track has no content, caller pauses the device
std::vector<std::vector<float>> TrackFreezer::renderTrack(Arrangement& arrangement, Mixer& mixer,
                                                           engine::Transport& transport, std::size_t trackIndex,
                                                           engine::Instrument* instrument, double sampleRate,
                                                           int blockSize, int numChannels) {
    Track& track = arrangement.track(trackIndex);

    const double tempo = transport.tempo();
    const double samplesPerTick = (60.0 / tempo) * sampleRate / static_cast<double>(kTicksPerQuarter);

    const SampleCount lastEnd = lastClipEndSamples(track, samplesPerTick);
    if (lastEnd <= 0) {
        return {};
    }

    const auto tailSamples = static_cast<SampleCount>(sampleRate);
    const SampleCount rawLength = lastEnd + tailSamples;
    const SampleCount numBlocks = (rawLength + blockSize - 1) / blockSize;
    const SampleCount totalFrames = numBlocks * blockSize;

    ChannelStrip& strip = mixer.trackStrip(trackIndex);
    engine::EffectChain& chain = strip.effects();
    for (std::size_t i = 0; i < chain.size(); ++i) {
        chain.at(i).reset();
    }

    std::vector<std::vector<float>> output(static_cast<std::size_t>(numChannels));

    std::vector<std::vector<float>> scratchBuffers(static_cast<std::size_t>(numChannels),
        std::vector<float>(static_cast<std::size_t>(blockSize), 0.0f));
    std::vector<float*> scratchPointers(static_cast<std::size_t>(numChannels));
    for (int channel = 0; channel < numChannels; ++channel) {
        scratchPointers[static_cast<std::size_t>(channel)] = scratchBuffers[static_cast<std::size_t>(channel)].data();
    }

    if (track.kind == TrackKind::Midi) {
        MidiTrackRenderer renderer(transport, track);
        renderer.prepare(sampleRate);
        renderer.setInstrument(instrument);

        SampleCount pos = 0;
        while (pos < totalFrames) {
            AudioBlock block { scratchPointers.data(), numChannels, blockSize };
            renderer.process(block, pos);
            strip.processEffects(block);

            for (int channel = 0; channel < numChannels; ++channel) {
                auto& channelOutput = output[static_cast<std::size_t>(channel)];
                channelOutput.insert(channelOutput.end(), scratchPointers[static_cast<std::size_t>(channel)],
                    scratchPointers[static_cast<std::size_t>(channel)] + blockSize);
            }

            pos += blockSize;
        }
    } else {
        AudioTrackRenderer renderer(transport, track);
        renderer.prepare(sampleRate);

        SampleCount pos = 0;
        while (pos < totalFrames) {
            AudioBlock block { scratchPointers.data(), numChannels, blockSize };
            renderer.process(block, pos);
            strip.processEffects(block);

            for (int channel = 0; channel < numChannels; ++channel) {
                auto& channelOutput = output[static_cast<std::size_t>(channel)];
                channelOutput.insert(channelOutput.end(), scratchPointers[static_cast<std::size_t>(channel)],
                    scratchPointers[static_cast<std::size_t>(channel)] + blockSize);
            }

            pos += blockSize;
        }
    }

    for (std::size_t i = 0; i < chain.size(); ++i) {
        chain.at(i).reset();
    }

    return output;
}

} // namespace howl::model
