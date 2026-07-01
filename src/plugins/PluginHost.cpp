// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: scans system VST3 plugins on a background thread, caches the result

#include "plugins/PluginHost.h"
#include "plugins/Vst3Adapter.h"

namespace howl::plugins {

namespace {
constexpr double kInitialSampleRate = 44100.0;
constexpr int kInitialBufferSize = 512;
} // namespace

// Loads a previously cached scan result, if one exists on disk
PluginHost::PluginHost() {
    m_formatManager.addDefaultFormats();

    if (auto xml = juce::XmlDocument::parse(cacheFilePath())) {
        m_knownPlugins.recreateFromXml(*xml);
        refreshDescriptors();
    }
}

// Joins the scan thread if one is still running
PluginHost::~PluginHost() {
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

// Scans for VST3 plugins on a background thread, caches results to disk when done
void PluginHost::rescan() {
    bool expected = false;
    if (!m_scanning.compare_exchange_strong(expected, true)) {
        return;
    }

    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }

    m_scanThread = std::thread([this] { scanThreadFunc(); });
}

// Returns the cached list from the most recent scan
std::vector<PluginDescriptor> PluginHost::list() const {
    std::lock_guard<std::mutex> lock(m_listMutex);
    return m_descriptors;
}

// Blocks the calling thread until any in-progress scan completes
void PluginHost::waitForScanToFinish() {
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

// Creates a VST3 instance for the given descriptor, matched against the cached scan
std::unique_ptr<IPluginInstance> PluginHost::instantiate(const PluginDescriptor& descriptor) {
    for (const auto& type : m_knownPlugins.getTypes()) {
        if (type.fileOrIdentifier.toStdString() != descriptor.path) {
            continue;
        }

        juce::String errorMessage;
        auto plugin = m_formatManager.createPluginInstance(type, kInitialSampleRate,
                                                            kInitialBufferSize, errorMessage);
        if (plugin == nullptr) {
            return nullptr;
        }

        return std::make_unique<Vst3Adapter>(std::move(plugin));
    }

    return nullptr;
}

// Runs the actual scan, called on m_scanThread
void PluginHost::scanThreadFunc() {
    juce::AudioPluginFormat* vst3Format = nullptr;
    for (auto* format : m_formatManager.getFormats()) {
        if (format->getName() == "VST3") {
            vst3Format = format;
            break;
        }
    }

    if (vst3Format != nullptr) {
        juce::PluginDirectoryScanner scanner(m_knownPlugins, *vst3Format,
                                              vst3Format->getDefaultLocationsToSearch(),
                                              true, juce::File());

        juce::String pluginBeingScanned;
        while (scanner.scanNextFile(true, pluginBeingScanned)) {
        }
    }

    refreshDescriptors();

    const juce::File cacheFile = cacheFilePath();
    cacheFile.getParentDirectory().createDirectory();
    if (auto xml = m_knownPlugins.createXml()) {
        xml->writeTo(cacheFile);
    }

    m_scanning.store(false, std::memory_order_release);
}

// Rebuilds m_descriptors from the current contents of m_knownPlugins
void PluginHost::refreshDescriptors() {
    std::vector<PluginDescriptor> descriptors;
    for (const auto& type : m_knownPlugins.getTypes()) {
        descriptors.push_back(PluginDescriptor {
            type.name.toStdString(),
            type.manufacturerName.toStdString(),
            type.pluginFormatName.toStdString(),
            type.fileOrIdentifier.toStdString(),
            type.isInstrument
        });
    }

    std::lock_guard<std::mutex> lock(m_listMutex);
    m_descriptors = std::move(descriptors);
}

// Path to the cached KnownPluginList XML
juce::File PluginHost::cacheFilePath() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Howl")
        .getChildFile("vst3_cache.xml");
}

} // namespace howl::plugins
