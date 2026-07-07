// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: sandboxed plugin host helper, child side of the shared memory audio protocol

#include "plugins/ShmAudioChannel.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

using howl::plugins::ShmAudioChannel;

namespace {

// Parsed command line: shared memory path, block geometry, and the mode to run in
struct HostArgs {
    juce::String shmPath;
    int numChannels = 2;
    int blockSize = 512;
    bool loopback = false;

    // Number of blocks to process normally before std::abort(), negative means never
    int crashAfter = -1;
};

// Reads --shm/--channels/--block/--loopback/--crash-after from argv, missing --shm is fatal
std::optional<HostArgs> parseArgs(int argc, char* argv[]) {
    HostArgs args;

    for (int i = 1; i < argc; ++i) {
        const juce::String arg(argv[i]);

        if (arg == "--shm" && i + 1 < argc) {
            args.shmPath = argv[++i];
        } else if (arg == "--channels" && i + 1 < argc) {
            args.numChannels = juce::String(argv[++i]).getIntValue();
        } else if (arg == "--block" && i + 1 < argc) {
            args.blockSize = juce::String(argv[++i]).getIntValue();
        } else if (arg == "--loopback") {
            args.loopback = true;
        } else if (arg == "--crash-after" && i + 1 < argc) {
            args.crashAfter = juce::String(argv[++i]).getIntValue();
        }
    }

    if (args.shmPath.isEmpty()) {
        return std::nullopt;
    }

    return args;
}

// True when a full line is waiting on stdin right now, without blocking to find out
bool lineWaitingOnStdin() {
   #if JUCE_WINDOWS
    const HANDLE stdinHandle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD bytesAvailable = 0;
    return PeekNamedPipe(stdinHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr) != 0 && bytesAvailable > 0;
   #else
    pollfd pfd { STDIN_FILENO, POLLIN, 0 };
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0;
   #endif
}

// Non blocking check for a {"cmd": "quit"} line on stdin, false when nothing is waiting.
// juce::ChildProcess never wires up the child's stdin (only stdout/stderr are piped), so
// stdin often reads as an immediate, permanent EOF here, that must not be mistaken for a
// quit request or the host would exit after its very first block whenever nobody bothered
// to send it commands
bool stdinRequestedQuit() {
    if (!lineWaitingOnStdin()) {
        return false;
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
        return false;
    }

    const juce::var parsed = juce::JSON::parse(line);
    return parsed.getProperty("cmd", juce::var()).toString() == "quit";
}

} // namespace

int main(int argc, char* argv[]) {
    const auto args = parseArgs(argc, argv);
    if (!args.has_value()) {
        std::fprintf(stderr, "howl-host: --shm <path> is required\n");
        return 1;
    }

    auto channel = ShmAudioChannel::open(juce::File(args->shmPath));
    if (channel == nullptr) {
        std::fprintf(stderr, "howl-host: failed to open shared memory at %s\n", args->shmPath.toRawUTF8());
        return 1;
    }

    int blocksProcessed = 0;

    while (channel->waitForInput()) {
        ++blocksProcessed;

        if (args->crashAfter >= 0 && blocksProcessed > args->crashAfter) {
            std::abort();
        }

        if (args->loopback) {
            float* const* in = channel->inputChannels();
            float* const* out = channel->outputChannels();
            for (int c = 0; c < args->numChannels; ++c) {
                std::memcpy(out[c], in[c], static_cast<size_t>(args->blockSize) * sizeof(float));
            }
        }

        channel->publishOutput();

        if (stdinRequestedQuit()) {
            break;
        }
    }

    return 0;
}
