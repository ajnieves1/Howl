// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: owns the default audio output device and drives an [RT] callback

#pragma once

#include "core/Types.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>

namespace howl::io {

// [RT] Runs on the audio thread, no allocation, no locks, no GUI calls
using AudioCallback = std::function<void(howl::AudioBlock&)>;

class AudioDevice : private juce::AudioIODeviceCallback {
public:
    // Constructs the device, unopened
    AudioDevice();

    // Closes the device if still open
    ~AudioDevice() override;

    // Opens the audio output device; restores savedState if given, otherwise opens the
    // default device. Returns false on failure
    bool open(const juce::XmlElement* savedState = nullptr);

    // Starts calling the callback on the audio thread once per block, call after open()
    void start(AudioCallback callback);

    // Stops the audio thread from calling back
    void stop();

    // Closes the device if it is open
    void close();

    // Valid after open(), 0.0 if the device is not open
    double getSampleRate() const;

    // Valid after open(), 0 if the device is not open
    int getBufferSize() const;

    // Number of buffer under/overruns reported by the OS, -1 if unsupported
    int getXRunCount() const;

    // Returns the underlying device manager, for the settings dialog
    juce::AudioDeviceManager& manager();

private:
    // Max output channels staged here, fixed so the [RT] callback never grows this array
    static constexpr int kMaxChannels = 32;

    // [RT] Fills one block of output via the user callback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    // Called once before the audio thread starts ticking
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;

    // Called once after the audio thread stops ticking
    void audioDeviceStopped() override;

    juce::AudioDeviceManager m_deviceManager;
    AudioCallback m_callback;
    float* m_channelPointers[kMaxChannels];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDevice)
};

} // namespace howl::io
