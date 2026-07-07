// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: runs on the parent, proxies one plugin inside a HowlHost child process

#pragma once

#include "plugins/IPluginInstance.h"
#include "plugins/PluginDescriptor.h"
#include "plugins/ShmAudioChannel.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace howl::plugins {

// Runs on the parent, proxies one plugin running in a HowlHost child process. Audio and
// note events cross the shared memory channel from T10, everything else (parameters, state,
// the editor) speaks JSON lines over a pipe to the child's stdin/stdout. Sandboxing is
// POSIX only (Linux and macOS); on other platforms isValid() stays false so the caller
// falls back to the in process adapter, same as any other failure to spawn
class SandboxedPluginInstance : public IPluginInstance {
public:
    // Spawns and handshakes the child for descriptor
    SandboxedPluginInstance(const PluginDescriptor& descriptor, double sampleRate, int blockSize);

    // Kills the child
    ~SandboxedPluginInstance() override;

    SandboxedPluginInstance(const SandboxedPluginInstance&) = delete;
    SandboxedPluginInstance& operator=(const SandboxedPluginInstance&) = delete;

    // True when the child spawned and answered the first ping
    bool isValid() const;

    // The child is already prepared at construction with the rate and block size it was
    // given, preparing again at a different rate would mean respawning the child, a known
    // gap documented in handoff.md rather than implemented here
    void prepare(double sampleRate, int maxBlockSize) override;

    // No-op, the child keeps running between the engine's prepare cycles
    void release() override;

    // [RT] midiIn must point to a const juce::MidiBuffer, may be nullptr
    void process(AudioBlock& audio, const void* midiIn) override;

    // Blocks up to 2 s for the child's reply, empty state on timeout or an invalid proxy
    StateBlob saveState() const override;

    void loadState(const StateBlob& state) override;

    const std::vector<ParamInfo>& params() const override;
    void setParamNormalized(uint32_t id, float value) override;

    // True once the child answered getParams, false for an invalid proxy
    bool hasEditor() const override;

    // Always nullptr, the child shows its own top level window rather than handing
    // back a Component that could cross the process boundary
    juce::Component* openEditor() override;
    void closeEditor() override;

private:
    // Writes one line to the child's stdin, no reply expected
    void sendLine(const juce::String& line) const;

    // Writes one line and blocks (up to timeoutMs) for the next line back, empty on timeout
    juce::String sendLineAndWaitForReply(const juce::String& line, int timeoutMs) const;

    int m_childStdinWriteFd = -1;
    int m_childStdoutReadFd = -1;
    long m_childPid = -1;

    std::unique_ptr<ShmAudioChannel> m_shmChannel;
    juce::File m_backingFile;

    bool m_valid = false;
    std::vector<ParamInfo> m_params;

    // Buffers partial reads between sendLineAndWaitForReply() calls, mutable so the
    // const state/param accessors can still pump the pipe
    mutable std::string m_readBuffer;
    mutable std::mutex m_ioMutex;
};

} // namespace howl::plugins
