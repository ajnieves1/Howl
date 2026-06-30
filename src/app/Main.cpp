// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: application entry point, main window, and test-tone wiring

#include "core/Types.h"
#include "engine/Graph.h"
#include "engine/Node.h"
#include "io/AudioDevice.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace hearth {

// [RT]-safe 440 Hz sine generator, prepare() must run before render() is called
class SineToneGenerator {
public:
    // Resets phase and computes the per-sample phase step
    void prepare(double sampleRate) {
        m_phase = 0.0;
        m_phaseIncrement = juce::MathConstants<double>::twoPi * kFrequencyHz / sampleRate;
    }

    // [RT] Fills every channel of the block with a sine wave
    void render(AudioBlock& block) noexcept {
        for (int frame = 0; frame < block.numFrames; ++frame) {
            const float sample = static_cast<float>(std::sin(m_phase)) * kAmplitude;
            for (int channel = 0; channel < block.numChannels; ++channel) {
                block.channels[channel][frame] = sample;
            }
            m_phase += m_phaseIncrement;
            if (m_phase >= juce::MathConstants<double>::twoPi) {
                m_phase -= juce::MathConstants<double>::twoPi;
            }
        }
    }

private:
    static constexpr double kFrequencyHz = 440.0;
    static constexpr float kAmplitude = 0.2f;

    double m_phase = 0.0;
    double m_phaseIncrement = 0.0;
};

// Wraps SineToneGenerator behind the Node interface so it can run inside a Graph
class ToneSourceNode : public engine::Node {
public:
    // Forwards to the wrapped generator
    void prepare(double sampleRate) {
        m_generator.prepare(sampleRate);
    }

    // [RT] Fills the block with the wrapped generator's sine wave
    void process(AudioBlock& audio, SampleCount) noexcept override {
        m_generator.render(audio);
    }

private:
    SineToneGenerator m_generator;
};

// Polls the xrun count once a second for 30 s and logs a pass/fail summary
class XrunWatcher : public juce::Timer {
public:
    // Stores the device to poll
    explicit XrunWatcher(io::AudioDevice& device)
        : m_device(device)
    {
    }

    // Starts polling once a second
    void start() {
        m_secondsElapsed = 0;
        startTimer(1000);
    }

    // Logs the xrun count and stops after kWatchDurationSeconds
    void timerCallback() override {
        ++m_secondsElapsed;
        const int xruns = m_device.getXRunCount();
        juce::Logger::writeToLog("Hearth: t=" + juce::String(m_secondsElapsed)
                                  + "s xrunCount=" + juce::String(xruns));

        if (m_secondsElapsed >= kWatchDurationSeconds) {
            stopTimer();
            const juce::String verdict = xruns == 0 ? "PASS" : "FAIL";
            juce::Logger::writeToLog("Hearth: " + verdict + " - xrunCount=" + juce::String(xruns)
                                      + " over " + juce::String(kWatchDurationSeconds) + "s");
        }
    }

private:
    static constexpr int kWatchDurationSeconds = 30;

    io::AudioDevice& m_device;
    int m_secondsElapsed = 0;
};

class MainWindow : public juce::DocumentWindow {
public:
    // Creates and shows a blank, resizable window
    MainWindow()
        : DocumentWindow(
              "Hearth",
              juce::Desktop::getInstance().getDefaultLookAndFeel()
                  .findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new juce::Component(), true);
        setResizable(true, true);
        centreWithSize(900, 600);
        setVisible(true);
    }

    // Requests app shutdown when the window's close button is clicked
    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class HearthApp : public juce::JUCEApplication {
public:
    // Returns the app's display name
    const juce::String getApplicationName() override
    {
        return "Hearth";
    }

    // Returns the app's version string
    const juce::String getApplicationVersion() override
    {
        return "0.1.0";
    }

    // Opens the main window, starts the audio device, and starts the xrun watcher
    void initialise(const juce::String&) override
    {
        m_mainWindow = std::make_unique<MainWindow>();

        if (!m_audioDevice.open()) {
            juce::Logger::writeToLog("Hearth: failed to open audio device");
            return;
        }

        auto toneSource = std::make_unique<ToneSourceNode>();
        toneSource->prepare(m_audioDevice.getSampleRate());
        m_graph.addNode(std::move(toneSource));
        m_graph.prepare();

        m_audioDevice.start([this](AudioBlock& block) {
            m_graph.process(block, 0);
        });

        m_xrunWatcher = std::make_unique<XrunWatcher>(m_audioDevice);
        m_xrunWatcher->start();
    }

    // Stops the xrun watcher, closes the audio device, and closes the window
    void shutdown() override
    {
        m_xrunWatcher.reset();
        m_audioDevice.close();
        m_mainWindow.reset();
    }

    // Quits the app when the OS or window asks it to
    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> m_mainWindow;
    io::AudioDevice m_audioDevice;
    engine::Graph m_graph;
    std::unique_ptr<XrunWatcher> m_xrunWatcher;
};

} // namespace hearth

START_JUCE_APPLICATION(hearth::HearthApp)
