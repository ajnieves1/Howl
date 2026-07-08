// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: runs on the parent, proxies one plugin inside a HowlHost child process

#include "plugins/SandboxedPluginInstance.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <chrono>
#include <cmath>
#include <cstring>

#if JUCE_LINUX || JUCE_MAC
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace howl::plugins {

namespace {
constexpr int kNumChannels = 2;
constexpr int kHandshakeTimeoutMs = 5000;
constexpr int kControlTimeoutMs = 2000;
} // namespace

// Runs one health tick, message thread only
SandboxedPluginInstance::HealthTimer::HealthTimer(SandboxedPluginInstance& owner)
    : m_owner(owner)
{
    startTimer(kHealthTickSeconds * 1000);
}

void SandboxedPluginInstance::HealthTimer::timerCallback() {
    m_owner.checkHealth();
}

// Spawns and handshakes the child for descriptor
SandboxedPluginInstance::SandboxedPluginInstance(const PluginDescriptor& descriptor, double sampleRate, int blockSize) {
    m_isInstrument = descriptor.isInstrument;

    juce::StringArray extraArgs;
    extraArgs.add("--format");
    extraArgs.add(descriptor.format);
    extraArgs.add("--path");
    extraArgs.add(descriptor.path);
    extraArgs.add("--name");
    extraArgs.add(descriptor.name);

    spawnChild(extraArgs, sampleRate, blockSize);
}

// Kills the child
SandboxedPluginInstance::~SandboxedPluginInstance() {
    teardownChild();
}

// Test seam: builds the proxy over an arbitrary helper command line
std::unique_ptr<SandboxedPluginInstance> SandboxedPluginInstance::createForTest(
    const juce::StringArray& args, double sampleRate, int blockSize) {
    auto instance = std::unique_ptr<SandboxedPluginInstance>(new SandboxedPluginInstance());
    instance->spawnChild(args, sampleRate, blockSize);
    return instance;
}

// Forks, wires the pipes, and maps a fresh shared memory channel, then runs the ping and
// getParams handshake. Shared by the constructor and restart()
bool SandboxedPluginInstance::spawnChild(const juce::StringArray& extraArgs, double sampleRate, int blockSize) {
   #if JUCE_LINUX || JUCE_MAC
    m_lastExtraArgs = extraArgs;
    m_lastSampleRate = sampleRate;
    m_lastBlockSize = blockSize;

    m_backingFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("howl_sandbox_" + juce::String(juce::Random::getSystemRandom().nextInt64()) + ".shm");

    auto channel = ShmAudioChannel::create(m_backingFile, kNumChannels, blockSize);
    if (channel == nullptr) {
        return false;
    }

    int stdinPipe[2];
    int stdoutPipe[2];
    if (pipe(stdinPipe) != 0) {
        return false;
    }
    if (pipe(stdoutPipe) != 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        return false;
    }

    const juce::String hostBinary = HOWL_HOST_BINARY;
    const juce::String shmPath = m_backingFile.getFullPathName();
    const juce::String channelsArg(kNumChannels);
    const juce::String blockArg(blockSize);
    const juce::String sampleRateArg(sampleRate, 0);

    const pid_t pid = fork();
    if (pid < 0) {
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return false;
    }

    if (pid == 0) {
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);

        // execvp needs a mutable, null-terminated argv, built from the fixed args above
        // plus whatever mode specific args the caller gave (a real plugin's format/path/
        // name, or a debug mode's --loopback/--crash-after)
        std::vector<char*> argv;
        auto push = [&argv](const juce::String& value) {
            argv.push_back(::strdup(value.toRawUTF8()));
        };
        push(hostBinary);
        push("--shm"); push(shmPath);
        push("--channels"); push(channelsArg);
        push("--block"); push(blockArg);
        push("--samplerate"); push(sampleRateArg);
        for (const auto& arg : extraArgs) {
            push(arg);
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    m_childStdinWriteFd = stdinPipe[1];
    m_childStdoutReadFd = stdoutPipe[0];
    m_childPid = pid;
    m_shmChannel = std::move(channel);
    close(stdinPipe[0]);
    close(stdoutPipe[1]);

    // The child boots the JUCE GUI and, for a plugin bridged through Wine, Wine itself
    // before it can answer anything, a generous handshake timeout absorbs a slow first load
    const juce::String pingReply = sendLineAndWaitForReply("{\"cmd\":\"ping\"}", kHandshakeTimeoutMs);
    m_valid = pingReply.contains("\"ok\"");

    if (!m_valid) {
        return false;
    }

    const juce::String paramsReply = sendLineAndWaitForReply("{\"cmd\":\"getParams\"}", kControlTimeoutMs);
    const juce::var parsed = juce::JSON::parse(paramsReply);
    m_params.clear();
    if (const auto* paramsArray = parsed.getProperty("params", juce::var()).getArray()) {
        for (const auto& entry : *paramsArray) {
            m_params.push_back(ParamInfo {
                static_cast<uint32_t>(static_cast<int>(entry.getProperty("id", 0))),
                entry.getProperty("name", juce::var()).toString().toStdString(),
                static_cast<float>(static_cast<double>(entry.getProperty("defaultNormalized", 0.0)))
            });
        }
    }

    m_ticksSinceSnapshot = 0;
    m_healthTimer = std::make_unique<HealthTimer>(*this);

    return true;
   #else
    juce::ignoreUnused(extraArgs, sampleRate, blockSize);
    return false;
   #endif
}

// Tears down the current child's process, pipes, and shared memory, if any
void SandboxedPluginInstance::teardownChild() {
   #if JUCE_LINUX || JUCE_MAC
    m_healthTimer.reset();

    if (m_childPid > 0) {
        kill(static_cast<pid_t>(m_childPid), SIGKILL);
        int status = 0;
        waitpid(static_cast<pid_t>(m_childPid), &status, 0);
    }
    if (m_childStdinWriteFd >= 0) {
        close(m_childStdinWriteFd);
    }
    if (m_childStdoutReadFd >= 0) {
        close(m_childStdoutReadFd);
    }

    m_childPid = -1;
    m_childStdinWriteFd = -1;
    m_childStdoutReadFd = -1;
    m_shmChannel.reset();
    m_backingFile.deleteFile();
    m_readBuffer.clear();
   #endif
}

// True when the child spawned and answered the first ping
bool SandboxedPluginInstance::isValid() const {
    return m_valid;
}

// True once the child died or stopped answering and process() latched the bypass
bool SandboxedPluginInstance::hasCrashed() const {
    return m_bypassed.load(std::memory_order_acquire);
}

// Kills whatever remains of the old child, respawns with the same arguments, and
// restores the last captured state
bool SandboxedPluginInstance::restart() {
   #if JUCE_LINUX || JUCE_MAC
    teardownChild();

    const bool spawned = spawnChild(m_lastExtraArgs, m_lastSampleRate, m_lastBlockSize);
    if (!spawned) {
        m_valid = false;
        return false;
    }

    if (!m_lastCapturedState.empty()) {
        loadState(m_lastCapturedState);
    }

    m_consecutiveFailures.store(0, std::memory_order_relaxed);
    m_bypassed.store(false, std::memory_order_release);
    return true;
   #else
    return false;
   #endif
}

// True when the child process is still alive, POSIX only
bool SandboxedPluginInstance::childProcessAlive() const {
   #if JUCE_LINUX || JUCE_MAC
    if (m_childPid <= 0) {
        return false;
    }
    return kill(static_cast<pid_t>(m_childPid), 0) == 0;
   #else
    return false;
   #endif
}

// Runs one health tick: liveness check, periodic state snapshot. Deliberately does not
// treat a stuck childAlive heartbeat alone as a hang: with the transport stopped or a
// silent block of playback, the parent legitimately sends no blocks for a while and the
// heartbeat naturally stalls with nothing wrong at all. A genuine hang still gets caught,
// just through process()'s own consecutive failure counter once blocks are actually
// being sent and stop getting answered, rather than through this timer
void SandboxedPluginInstance::checkHealth() {
    if (!m_valid || m_bypassed.load(std::memory_order_acquire)) {
        return;
    }

    if (!childProcessAlive()) {
        m_bypassed.store(true, std::memory_order_release);
        return;
    }

    ++m_ticksSinceSnapshot;
    if (m_ticksSinceSnapshot >= kSnapshotEveryNTicks) {
        m_ticksSinceSnapshot = 0;
        m_lastCapturedState = saveState();
    }
}

// The child is already prepared at construction, but PluginHost::instantiate() spawns it
// at a fixed placeholder rate and block size before the real audio device is known, so a
// mismatch here is the normal case, not an edge case, on any device that does not happen
// to run at exactly that placeholder. When the values actually differ, respawn the child
// at the right ones and carry its current state across so the patch is not lost
void SandboxedPluginInstance::prepare(double sampleRate, int maxBlockSize) {
    constexpr double kSampleRateEpsilon = 0.5;
    const bool sameSampleRate = std::abs(sampleRate - m_lastSampleRate) < kSampleRateEpsilon;

    if (!m_valid || (sameSampleRate && maxBlockSize == m_lastBlockSize)) {
        return;
    }

    const StateBlob state = saveState();
    teardownChild();

    if (spawnChild(m_lastExtraArgs, sampleRate, maxBlockSize)) {
        if (!state.empty()) {
            loadState(state);
        }
        m_consecutiveFailures.store(0, std::memory_order_relaxed);
        m_bypassed.store(false, std::memory_order_release);
    }
}

// No-op, the child keeps running between the engine's prepare cycles
void SandboxedPluginInstance::release() {
}

// [RT] midiIn must point to a const juce::MidiBuffer, may be nullptr. Bypassed or
// crashed: instruments go silent, effects pass audio through unchanged
void SandboxedPluginInstance::process(AudioBlock& audio, const void* midiIn) {
    if (!m_valid || m_shmChannel == nullptr || m_bypassed.load(std::memory_order_acquire)) {
        if (m_isInstrument) {
            for (int c = 0; c < audio.numChannels; ++c) {
                std::memset(audio.channels[c], 0, static_cast<size_t>(audio.numFrames) * sizeof(float));
            }
        }
        // Effects: audio.channels already holds the caller's input, leave it as is
        return;
    }

    MidiEvent events[ShmAudioChannel::kMaxEvents];
    int numEvents = 0;

    if (midiIn != nullptr) {
        const auto* midi = static_cast<const juce::MidiBuffer*>(midiIn);
        for (const auto metadata : *midi) {
            if (numEvents >= ShmAudioChannel::kMaxEvents) {
                break;
            }
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                events[numEvents++] = MidiEvent { MidiEvent::Type::NoteOn, message.getNoteNumber(), message.getFloatVelocity() };
            } else if (message.isNoteOff()) {
                events[numEvents++] = MidiEvent { MidiEvent::Type::NoteOff, message.getNoteNumber(), 0.0f };
            }
        }
    }

    const bool ok = m_shmChannel->exchange(audio, events, numEvents);

    if (ok) {
        m_consecutiveFailures.store(0, std::memory_order_relaxed);
        return;
    }

    const int failures = m_consecutiveFailures.fetch_add(1, std::memory_order_relaxed) + 1;
    if (failures >= kMaxConsecutiveFailures) {
        m_bypassed.store(true, std::memory_order_release);
    }
}

// Blocks up to 2 s for the child's reply, empty state on timeout or an invalid proxy.
// Caches the result as the most recent snapshot for a later restart()
StateBlob SandboxedPluginInstance::saveState() const {
    if (!m_valid) {
        return {};
    }

    const juce::String reply = sendLineAndWaitForReply("{\"cmd\":\"getState\"}", kControlTimeoutMs);
    const juce::var parsed = juce::JSON::parse(reply);
    const juce::String base64 = parsed.getProperty("stateBase64", juce::var()).toString();

    // Base64::toBase64() on the encode side pairs with Base64::convertFromBase64() here,
    // NOT MemoryBlock::fromBase64Encoding(), which expects its own "byteCount.base64" format
    // and otherwise just silently fails and leaves an empty block
    juce::MemoryOutputStream decoded;
    juce::Base64::convertFromBase64(decoded, base64);
    const auto* bytes = static_cast<const uint8_t*>(decoded.getData());
    const StateBlob state(bytes, bytes + decoded.getDataSize());

    if (!state.empty()) {
        const_cast<SandboxedPluginInstance*>(this)->m_lastCapturedState = state;
    }

    return state;
}

void SandboxedPluginInstance::loadState(const StateBlob& state) {
    if (!m_valid) {
        return;
    }

    const juce::String base64 = juce::Base64::toBase64(state.data(), state.size());
    juce::String line = "{\"cmd\":\"setState\",\"stateBase64\":\"";
    line << base64 << "\"}";
    sendLine(line);
}

const std::vector<ParamInfo>& SandboxedPluginInstance::params() const {
    return m_params;
}

void SandboxedPluginInstance::setParamNormalized(uint32_t id, float value) {
    if (!m_valid) {
        return;
    }

    juce::String line = "{\"cmd\":\"setParam\",\"index\":";
    line << static_cast<int>(id) << ",\"value\":" << value << "}";
    sendLine(line);
}

// True once the child answered getParams, false for an invalid proxy
bool SandboxedPluginInstance::hasEditor() const {
    return m_valid;
}

// Always nullptr, the child shows its own top level window. Snapshots state first, per
// the restart contract, so a crash while the editor is open loses as little as possible
juce::Component* SandboxedPluginInstance::openEditor() {
    if (m_valid) {
        const StateBlob snapshot = saveState();
        if (!snapshot.empty()) {
            m_lastCapturedState = snapshot;
        }
    }

    sendLine("{\"cmd\":\"openEditor\"}");
    return nullptr;
}

void SandboxedPluginInstance::closeEditor() {
    sendLine("{\"cmd\":\"closeEditor\"}");
}

// Writes one line to the child's stdin, no reply expected
void SandboxedPluginInstance::sendLine(const juce::String& line) const {
   #if JUCE_LINUX || JUCE_MAC
    if (m_childStdinWriteFd < 0) {
        return;
    }
    const juce::String withNewline = line + "\n";
    const char* utf8 = withNewline.toRawUTF8();
    const auto length = std::strlen(utf8);
    std::size_t written = 0;
    while (written < length) {
        const ssize_t result = write(m_childStdinWriteFd, utf8 + written, length - written);
        if (result <= 0) {
            break;
        }
        written += static_cast<std::size_t>(result);
    }
   #else
    juce::ignoreUnused(line);
   #endif
}

// Writes one line and blocks (up to timeoutMs) for the next line back, empty on timeout
juce::String SandboxedPluginInstance::sendLineAndWaitForReply(const juce::String& line, int timeoutMs) const {
   #if JUCE_LINUX || JUCE_MAC
    std::lock_guard<std::mutex> lock(m_ioMutex);
    sendLine(line);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    for (;;) {
        const auto newlinePos = m_readBuffer.find('\n');
        if (newlinePos != std::string::npos) {
            const std::string result = m_readBuffer.substr(0, newlinePos);
            m_readBuffer.erase(0, newlinePos + 1);
            return juce::String(result);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return {};
        }

        const int remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

        pollfd pfd { m_childStdoutReadFd, POLLIN, 0 };
        if (poll(&pfd, 1, remainingMs) <= 0) {
            continue;
        }

        char buffer[4096];
        const ssize_t bytesRead = read(m_childStdoutReadFd, buffer, sizeof(buffer));
        if (bytesRead <= 0) {
            return {};
        }
        m_readBuffer.append(buffer, static_cast<std::size_t>(bytesRead));
    }
   #else
    juce::ignoreUnused(line, timeoutMs);
    return {};
   #endif
}

} // namespace howl::plugins
