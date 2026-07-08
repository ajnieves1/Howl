// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: SandboxProtocolTests, exercises ShmAudioChannel against the real howl-host helper

#include "plugins/SandboxedPluginInstance.h"
#include "plugins/ShmAudioChannel.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

using howl::AudioBlock;
using howl::plugins::SandboxedPluginInstance;
using howl::plugins::ShmAudioChannel;

namespace {

constexpr int kNumChannels = 2;
constexpr int kBlockSize = 128;

// A fresh, unique backing file path for one test's shared memory
juce::File makeBackingFile() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("howl_shm_test_" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".bin");
}

// Launches howl-host against the given backing file with the given extra arguments
std::unique_ptr<juce::ChildProcess> launchHost(const juce::File& backingFile, const juce::StringArray& extraArgs) {
    juce::StringArray command;
    command.add(HOWL_HOST_BINARY);
    command.add("--shm");
    command.add(backingFile.getFullPathName());
    command.add("--channels");
    command.add(juce::String(kNumChannels));
    command.add("--block");
    command.add(juce::String(kBlockSize));
    command.addArray(extraArgs);

    auto process = std::make_unique<juce::ChildProcess>();
    if (!process->start(command)) {
        return nullptr;
    }
    return process;
}

// Fills a block's channels with an ascending ramp so a loopback round trip is easy to verify
void fillRamp(AudioBlock& block, float startValue) {
    for (int c = 0; c < block.numChannels; ++c) {
        for (int i = 0; i < block.numFrames; ++i) {
            block.channels[c][i] = startValue + static_cast<float>(i);
        }
    }
}

// Reads whatever the child printed to stdout/stderr, for diagnosing a CI-only failure
// that cannot be reproduced or debugged locally
juce::String childOutput(juce::ChildProcess& process) {
    return process.readAllProcessOutput();
}

// True when every sample in every channel of the block is exactly 0
bool isSilent(const AudioBlock& block) {
    for (int c = 0; c < block.numChannels; ++c) {
        for (int i = 0; i < block.numFrames; ++i) {
            if (block.channels[c][i] != 0.0f) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("ShmAudioChannel round-trips 64 ramp blocks through a real loopback child", "[sandbox]") {
    const juce::File backingFile = makeBackingFile();
    auto parentChannel = ShmAudioChannel::create(backingFile, kNumChannels, kBlockSize);
    REQUIRE(parentChannel != nullptr);

    auto host = launchHost(backingFile, { "--loopback" });
    REQUIRE(host != nullptr);

    // Gives the freshly spawned process a moment to open the shared memory before the
    // first exchange, well under the deadline but generous against process start jitter
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    float left[kBlockSize];
    float right[kBlockSize];
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    for (int i = 0; i < 64; ++i) {
        fillRamp(block, static_cast<float>(i));

        float expectedLeft[kBlockSize];
        float expectedRight[kBlockSize];
        std::memcpy(expectedLeft, left, sizeof(expectedLeft));
        std::memcpy(expectedRight, right, sizeof(expectedRight));

        const bool exchanged = parentChannel->exchange(block, nullptr, 0);
        if (!exchanged) {
            // Diagnostic only: this failure is not reproducible locally, so capture
            // whatever the child printed before failing the assertion
            host->kill();
            FAIL("exchange() failed on iteration " << i << ", child output:\n" << childOutput(*host));
        }

        REQUIRE(std::memcmp(left, expectedLeft, sizeof(expectedLeft)) == 0);
        REQUIRE(std::memcmp(right, expectedRight, sizeof(expectedRight)) == 0);
    }

    host->kill();
    backingFile.deleteFile();
}

TEST_CASE("ShmAudioChannel times out and returns silence once the child crashes", "[sandbox]") {
    const juce::File backingFile = makeBackingFile();
    auto parentChannel = ShmAudioChannel::create(backingFile, kNumChannels, kBlockSize);
    REQUIRE(parentChannel != nullptr);

    auto host = launchHost(backingFile, { "--loopback", "--crash-after", "10" });
    REQUIRE(host != nullptr);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    float left[kBlockSize];
    float right[kBlockSize];
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    for (int i = 0; i < 10; ++i) {
        fillRamp(block, static_cast<float>(i));
        REQUIRE(parentChannel->exchange(block, nullptr, 0));
    }

    // Block 11 lands after the crash, exchange() must give up at the deadline rather
    // than hang, and the caller gets silence instead of stale or garbage audio
    fillRamp(block, 99.0f);
    const auto start = std::chrono::steady_clock::now();
    const bool ok = parentChannel->exchange(block, nullptr, 0);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(ok);
    REQUIRE(isSilent(block));
    REQUIRE(elapsed < std::chrono::milliseconds(50));

    // A second call must behave the same way, the dead child does not un-stick anything
    fillRamp(block, 42.0f);
    REQUIRE_FALSE(parentChannel->exchange(block, nullptr, 0));
    REQUIRE(isSilent(block));

    host->kill();
    backingFile.deleteFile();
}

TEST_CASE("ShmAudioChannel does not hang when the child is killed mid-run", "[sandbox]") {
    const juce::File backingFile = makeBackingFile();
    auto parentChannel = ShmAudioChannel::create(backingFile, kNumChannels, kBlockSize);
    REQUIRE(parentChannel != nullptr);

    auto host = launchHost(backingFile, { "--loopback" });
    REQUIRE(host != nullptr);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    float left[kBlockSize];
    float right[kBlockSize];
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    for (int i = 0; i < 5; ++i) {
        fillRamp(block, static_cast<float>(i));
        REQUIRE(parentChannel->exchange(block, nullptr, 0));
    }

    REQUIRE(host->kill());

    fillRamp(block, 7.0f);
    const auto start = std::chrono::steady_clock::now();
    const bool ok = parentChannel->exchange(block, nullptr, 0);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(ok);
    REQUIRE(isSilent(block));
    REQUIRE(elapsed < std::chrono::milliseconds(50));

    backingFile.deleteFile();
}

TEST_CASE("SandboxedPluginInstance flips hasCrashed after the child dies, bounded and passthrough", "[sandbox]") {
    auto instance = SandboxedPluginInstance::createForTest({ "--loopback", "--crash-after", "20" }, 44100.0, kBlockSize);
    REQUIRE(instance != nullptr);
    REQUIRE(instance->isValid());
    REQUIRE_FALSE(instance->hasCrashed());

    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    // The child dies after 20 good blocks, then process() needs 100 consecutive failed
    // exchanges before it latches the bypass, driving well past that either way
    const auto start = std::chrono::steady_clock::now();
    int iterations = 0;
    while (!instance->hasCrashed() && iterations < 200) {
        instance->process(block, nullptr);
        ++iterations;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(instance->hasCrashed());
    REQUIRE(iterations < 200);
    // Each failed exchange gives up at a 2 ms deadline, so even ~100 of them in a row
    // stays well under a second, proving process() never hangs on a dead child
    REQUIRE(elapsed < std::chrono::seconds(2));

    // Bypassed now: an effect proxy (createForTest's default) passes audio through
    // unchanged rather than calling exchange() again
    channels[0][0] = 0.42f;
    channels[1][0] = 0.24f;
    instance->process(block, nullptr);
    REQUIRE(channels[0][0] == 0.42f);
    REQUIRE(channels[1][0] == 0.24f);
}

TEST_CASE("SandboxedPluginInstance.restart respawns the child and resumes exchanging", "[sandbox]") {
    auto instance = SandboxedPluginInstance::createForTest({ "--loopback", "--crash-after", "5" }, 44100.0, kBlockSize);
    REQUIRE(instance != nullptr);
    REQUIRE(instance->isValid());

    float left[kBlockSize] = {};
    float right[kBlockSize] = {};
    float* channels[kNumChannels] = { left, right };
    AudioBlock block { channels, kNumChannels, kBlockSize };

    int iterations = 0;
    while (!instance->hasCrashed() && iterations < 200) {
        instance->process(block, nullptr);
        ++iterations;
    }
    REQUIRE(instance->hasCrashed());

    REQUIRE(instance->restart());
    REQUIRE_FALSE(instance->hasCrashed());
    REQUIRE(instance->isValid());

    fillRamp(block, 11.0f);
    float expectedLeft[kBlockSize];
    float expectedRight[kBlockSize];
    std::memcpy(expectedLeft, left, sizeof(expectedLeft));
    std::memcpy(expectedRight, right, sizeof(expectedRight));

    instance->process(block, nullptr);

    REQUIRE(std::memcmp(left, expectedLeft, sizeof(expectedLeft)) == 0);
    REQUIRE(std::memcmp(right, expectedRight, sizeof(expectedRight)) == 0);
}
