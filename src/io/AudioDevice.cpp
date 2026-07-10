// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: owns the default audio output device and drives an [RT] callback

#include "io/AudioDevice.h"

namespace howl::io {

// Zeroes the channel pointer array
AudioDevice::AudioDevice() {
    for (int i = 0; i < kMaxChannels; ++i) {
        m_channelPointers[i] = nullptr;
    }
}

// Closes the device if still open
AudioDevice::~AudioDevice() {
    close();
}

// Initialises the audio devices, restoring savedState if given
bool AudioDevice::open(const juce::XmlElement* savedState) {
    const juce::String error = m_deviceManager.initialise(0, 2, savedState, true);
    return error.isEmpty();
}

// Stores the callback and registers this as the device's audio callback
void AudioDevice::start(AudioCallback callback) {
    m_callback = std::move(callback);
    m_deviceManager.addAudioCallback(this);
}

// Unregisters this as the device's audio callback
void AudioDevice::stop() {
    m_deviceManager.removeAudioCallback(this);
}

// Stops the callback and releases the device
void AudioDevice::close() {
    stop();
    m_deviceManager.closeAudioDevice();
}

// Reads the current device's sample rate
double AudioDevice::getSampleRate() const {
    if (auto* device = m_deviceManager.getCurrentAudioDevice()) {
        return device->getCurrentSampleRate();
    }
    return 0.0;
}

// Reads the current device's buffer size
int AudioDevice::getBufferSize() const {
    if (auto* device = m_deviceManager.getCurrentAudioDevice()) {
        return device->getCurrentBufferSizeSamples();
    }
    return 0;
}

// Reads the current device's reported xrun count
int AudioDevice::getXRunCount() const {
    if (auto* device = m_deviceManager.getCurrentAudioDevice()) {
        return device->getXRunCount();
    }
    return -1;
}

// Returns the underlying device manager, for the settings dialog
juce::AudioDeviceManager& AudioDevice::manager() {
    return m_deviceManager;
}

// [RT] Fills one block of output via the user callback
void AudioDevice::audioDeviceIOCallbackWithContext(const float* const* /*inputChannelData*/,
                                                    int /*numInputChannels*/,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& /*context*/) {
    if (outputChannelData == nullptr || numOutputChannels <= 0) {
        return;
    }

    const int channelsToUse = juce::jmin(numOutputChannels, kMaxChannels);

    // Stage pointers into our own fixed-size array, no allocation since kMaxChannels is a compile-time bound
    for (int i = 0; i < channelsToUse; ++i) {
        m_channelPointers[i] = outputChannelData[i];
    }

    if (!m_callback) {
        for (int i = 0; i < channelsToUse; ++i) {
            juce::FloatVectorOperations::clear(m_channelPointers[i], numSamples);
        }
        return;
    }

    howl::AudioBlock block { m_channelPointers, channelsToUse, numSamples };
    m_callback(block);
}

// Nothing to do: sample rate is read on demand via getSampleRate()
void AudioDevice::audioDeviceAboutToStart(juce::AudioIODevice* /*device*/) {
}

// Nothing to do
void AudioDevice::audioDeviceStopped() {
}

} // namespace howl::io
