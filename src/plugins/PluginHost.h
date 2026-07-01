// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: scans system VST3 plugins on a background thread, caches the result

#pragma once

#include "plugins/IPluginInstance.h"
#include "plugins/PluginDescriptor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace howl::plugins {

class PluginHost : public IPluginHost {
public:
    // Loads a previously cached scan result, if one exists on disk
    PluginHost();

    // Joins the scan thread if one is still running
    ~PluginHost() override;

    // Scans for VST3 plugins on a background thread, caches results to disk when done
    void rescan() override;

    // Returns the cached list from the most recent scan
    std::vector<PluginDescriptor> list() const override;

    // Blocks the calling thread until any in-progress scan completes
    void waitForScanToFinish();

    // Not yet implemented, wired up when the VST3 adapter is built
    std::unique_ptr<IPluginInstance> instantiate(const PluginDescriptor& descriptor) override;

private:
    // Runs the actual scan, called on m_scanThread
    void scanThreadFunc();

    // Rebuilds m_descriptors from the current contents of m_knownPlugins
    void refreshDescriptors();

    // Path to the cached KnownPluginList XML
    static juce::File cacheFilePath();

    juce::AudioPluginFormatManager m_formatManager;
    juce::KnownPluginList m_knownPlugins;

    mutable std::mutex m_listMutex;
    std::vector<PluginDescriptor> m_descriptors;

    std::thread m_scanThread;
    std::atomic<bool> m_scanning { false };
};

} // namespace howl::plugins
