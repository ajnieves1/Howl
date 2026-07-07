// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: scans system VST3 plugins on a background thread, caches the result

#pragma once

#include "plugins/ClapAdapter.h"
#include "plugins/IPluginInstance.h"
#include "plugins/PluginDescriptor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace howl::plugins {

// Scans and instantiates both VST3 (cached to disk between launches) and CLAP
// (rescanned every launch, few enough files that a cache is not worth it) plugins
class PluginHost : public IPluginHost {
public:
    // Loads a previously cached scan result, if one exists on disk
    PluginHost();

    // Joins the scan thread if one is still running
    ~PluginHost() override;

    // Scans for VST3 and CLAP plugins on a background thread, caches the VST3 result to disk
    void rescan() override;

    // Returns the cached list from the most recent scan
    std::vector<PluginDescriptor> list() const override;

    // Blocks the calling thread until any in-progress scan completes
    void waitForScanToFinish();

    // Instantiates a VST3 or CLAP plugin, matched against the cached scan by format
    std::unique_ptr<IPluginInstance> instantiate(const PluginDescriptor& descriptor) override;

private:
    // Runs the actual scan, called on m_scanThread
    void scanThreadFunc();

    // Rebuilds m_descriptors from the current contents of m_knownPlugins and m_clapPlugins
    void refreshDescriptors();

    // Path to the cached KnownPluginList XML
    static juce::File cacheFilePath();

    juce::AudioPluginFormatManager m_formatManager;
    juce::KnownPluginList m_knownPlugins;

    mutable std::mutex m_listMutex;
    std::vector<PluginDescriptor> m_descriptors;
    std::vector<ClapPluginInfo> m_clapPlugins;

    std::thread m_scanThread;
    std::atomic<bool> m_scanning { false };
};

} // namespace howl::plugins
