// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: runs on the parent, proxies one plugin inside a HowlHost child process

#pragma once

#include "plugins/IPluginInstance.h"
#include "plugins/PluginDescriptor.h"
#include "plugins/ShmAudioChannel.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace howl::plugins {

// Runs on the parent, proxies one plugin running in a HowlHost child process. Audio and
// note events cross the shared memory channel from T10, everything else (parameters, state,
// the editor) speaks JSON lines over a pipe to the child's stdin/stdout. Sandboxing is
// POSIX only (Linux and macOS); on other platforms isValid() stays false so the caller
// falls back to the in process adapter, same as any other failure to spawn.
//
// Crash handling: process() counts consecutive exchange() failures and latches a bypass
// flag after too many, at which point effects pass audio through unchanged and instruments
// go silent rather than keep spending the exchange deadline on a dead child. A message
// thread timer separately polls the child process's liveness once a second (a stuck
// heartbeat alone is not treated as a hang, silence or a stopped transport look the same
// and are not crashes) and snapshots state every 30 ticks while healthy, so a restart has
// something recent to restore
class SandboxedPluginInstance : public IPluginInstance {
public:
    // Spawns and handshakes the child for descriptor
    SandboxedPluginInstance(const PluginDescriptor& descriptor, double sampleRate, int blockSize);

    // Kills the child
    ~SandboxedPluginInstance() override;

    SandboxedPluginInstance(const SandboxedPluginInstance&) = delete;
    SandboxedPluginInstance& operator=(const SandboxedPluginInstance&) = delete;

    // Test seam: builds the proxy over an arbitrary helper command line (the T10 debug
    // modes, --loopback and --crash-after, rather than a real plugin), same handshake
    static std::unique_ptr<SandboxedPluginInstance> createForTest(const juce::StringArray& args,
                                                                  double sampleRate, int blockSize);

    // True when the child spawned and answered the first ping
    bool isValid() const;

    // True once the child died or stopped answering and process() latched the bypass
    bool hasCrashed() const;

    // Kills whatever remains of the old child, respawns with the same arguments, and
    // restores the last captured state. Returns false (still bypassed) if the respawn
    // or handshake fails
    bool restart();

    // Waits until the child has answered every audio exchange issued so far, sleeping
    // between checks. Message thread only. False when timeoutMs passes first or no child
    // channel exists
    bool waitForChildIdle(int timeoutMs) const;

    // The child is already prepared at construction, but usually with a placeholder rate
    // and block size, not the real device's. When the given values actually differ,
    // respawns the child at the right ones and carries its state across
    void prepare(double sampleRate, int maxBlockSize) override;

    // No-op, the child keeps running between the engine's prepare cycles
    void release() override;

    // [RT] midiIn must point to a const juce::MidiBuffer, may be nullptr. Bypassed or
    // crashed: instruments go silent, effects pass audio through unchanged
    void process(AudioBlock& audio, const void* midiIn) override;

    // Blocks up to 2 s for the child's reply, empty state on timeout or an invalid proxy.
    // Caches the result as the most recent snapshot for a later restart()
    StateBlob saveState() const override;

    void loadState(const StateBlob& state) override;

    const std::vector<ParamInfo>& params() const override;
    void setParamNormalized(uint32_t id, float value) override;

    // True once the child answered getParams, false for an invalid proxy
    bool hasEditor() const override;

    // Always nullptr, the child shows its own top level window rather than handing
    // back a Component that could cross the process boundary. Snapshots state first,
    // per the restart contract, so a crash while the editor is open loses as little as
    // possible
    juce::Component* openEditor() override;
    void closeEditor() override;

private:
    // Default constructed, only createForTest() and restart() use this path
    SandboxedPluginInstance() = default;

    // Forks, wires the pipes, and maps a fresh shared memory channel, then runs the ping
    // and getParams handshake. Shared by the constructor and restart()
    bool spawnChild(const juce::StringArray& extraArgs, double sampleRate, int blockSize);

    // Tears down the current child's process, pipes, and shared memory, if any
    void teardownChild();

    // Writes one line to the child's stdin, no reply expected
    void sendLine(const juce::String& line) const;

    // Writes one line and blocks (up to timeoutMs) for the next line back, empty on timeout
    juce::String sendLineAndWaitForReply(const juce::String& line, int timeoutMs) const;

    // Message thread timer: checks the child process is still running once a second,
    // and snapshots state every 30 ticks while healthy
    class HealthTimer : public juce::Timer {
    public:
        explicit HealthTimer(SandboxedPluginInstance& owner);
        void timerCallback() override;

    private:
        SandboxedPluginInstance& m_owner;
    };

    // Runs one health tick: liveness check, periodic state snapshot
    void checkHealth();

    // True when the child process is still alive, POSIX only
    bool childProcessAlive() const;

    static constexpr int kMaxConsecutiveFailures = 100;
    static constexpr int kHealthTickSeconds = 1;
    static constexpr int kSnapshotEveryNTicks = 30;

    int m_childStdinWriteFd = -1;
    int m_childStdoutReadFd = -1;
    long m_childPid = -1;

    std::unique_ptr<ShmAudioChannel> m_shmChannel;
    juce::File m_backingFile;

    bool m_valid = false;
    bool m_isInstrument = false;
    std::vector<ParamInfo> m_params;

    // Remembered so restart() can respawn identically to the original spawn
    juce::StringArray m_lastExtraArgs;
    double m_lastSampleRate = 44100.0;
    int m_lastBlockSize = 512;

    std::atomic<int> m_consecutiveFailures { 0 };
    std::atomic<bool> m_bypassed { false };

    // Message thread only: the timer itself, ticks since the last snapshot, and the
    // most recent state snapshot restart() falls back to
    std::unique_ptr<HealthTimer> m_healthTimer;
    int m_ticksSinceSnapshot = 0;
    StateBlob m_lastCapturedState;

    // Buffers partial reads between sendLineAndWaitForReply() calls, mutable so the
    // const state/param accessors can still pump the pipe
    mutable std::string m_readBuffer;
    mutable std::mutex m_ioMutex;
};

} // namespace howl::plugins
