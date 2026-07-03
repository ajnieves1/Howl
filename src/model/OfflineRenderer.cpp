// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders an Arrangement faster than real time to a wav file

#include "model/OfflineRenderer.h"

#include "io/AudioFile.h"
#include "model/ArrangementNode.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

namespace howl::model {

// Renders arrangement through transport for numFrames, faster than real
// time, block by block, and writes the result to a wav at path, starts
// the transport playing and returns false if the file could not be opened
bool OfflineRenderer::renderToFile(Arrangement& arrangement, engine::Transport& transport, double sampleRate,
                                   int blockSize, int numChannels, SampleCount numFrames, const std::string& path) {
    io::AudioFileWriter writer;
    if (!writer.open(path, sampleRate, numChannels)) {
        return false;
    }

    ArrangementNode node(transport, arrangement);
    node.prepare(sampleRate, blockSize, numChannels);
    transport.play();

    std::vector<std::vector<float>> buffers(static_cast<std::size_t>(numChannels),
                                             std::vector<float>(static_cast<std::size_t>(blockSize), 0.0f));
    std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
    for (std::size_t channel = 0; channel < channelPointers.size(); ++channel) {
        channelPointers[channel] = buffers[channel].data();
    }

    SampleCount framesRemaining = numFrames;
    while (framesRemaining > 0) {
        const auto framesThisBlock = static_cast<int>(framesRemaining < blockSize ? framesRemaining : blockSize);
        const SampleCount pos = transport.advance(framesThisBlock);

        AudioBlock block { channelPointers.data(), numChannels, framesThisBlock };
        node.process(block, pos);
        writer.write(block);

        framesRemaining -= framesThisBlock;
    }

    writer.close();
    return true;
}

// Renders the prepared node from sample 0 to lengthSamples into a 24-bit WAV, device must be paused
bool OfflineRenderer::renderNodeToFile(ArrangementNode& node, engine::Transport& transport, double sampleRate,
                                        int blockSize, int numChannels, SampleCount lengthSamples,
                                        const juce::File& outFile) {
    auto stream = std::make_unique<juce::FileOutputStream>(outFile);
    if (stream->failedToOpen()) {
        return false;
    }

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        stream.get(), sampleRate, static_cast<unsigned int>(numChannels), 24, {}, 0));
    if (writer == nullptr) {
        return false;
    }

    // createWriterFor() took ownership of the stream on success
    stream.release();

    const SampleCount savedPosition = transport.position();
    const SampleCount savedLoopStart = transport.loopStart();
    const SampleCount savedLoopEnd = transport.loopEnd();
    const bool savedLoopEnabled = transport.loopEnabled();

    transport.setLoop(savedLoopStart, savedLoopEnd, false);
    transport.setPosition(0);
    transport.play();

    std::vector<std::vector<float>> buffers(static_cast<std::size_t>(numChannels),
                                             std::vector<float>(static_cast<std::size_t>(blockSize), 0.0f));
    std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
    for (std::size_t channel = 0; channel < channelPointers.size(); ++channel) {
        channelPointers[channel] = buffers[channel].data();
    }

    SampleCount framesRemaining = lengthSamples;
    while (framesRemaining > 0) {
        const auto framesThisBlock = static_cast<int>(framesRemaining < blockSize ? framesRemaining : blockSize);
        const SampleCount pos = transport.advance(framesThisBlock);

        AudioBlock block { channelPointers.data(), numChannels, framesThisBlock };
        node.process(block, pos);
        writer->writeFromFloatArrays(block.channels, block.numChannels, block.numFrames);

        framesRemaining -= framesThisBlock;
    }

    writer.reset(); // flushes and closes

    transport.stop();
    transport.setLoop(savedLoopStart, savedLoopEnd, savedLoopEnabled);
    transport.setPosition(savedPosition);

    return true;
}

} // namespace howl::model
