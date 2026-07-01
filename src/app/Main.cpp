// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: application entry point, main window, and piano-roll wiring

#include "core/Types.h"
#include "dsp/SubtractiveSynth.h"
#include "engine/Graph.h"
#include "engine/Node.h"
#include "engine/Transport.h"
#include "io/AudioDevice.h"
#include "model/MidiClip.h"
#include "model/SequencerNode.h"
#include "ui/PianoRoll.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace howl {

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
        juce::Logger::writeToLog("Howl: t=" + juce::String(m_secondsElapsed)
                                  + "s xrunCount=" + juce::String(xruns));

        if (m_secondsElapsed >= kWatchDurationSeconds) {
            stopTimer();
            const juce::String verdict = xruns == 0 ? "PASS" : "FAIL";
            juce::Logger::writeToLog("Howl: " + verdict + " - xrunCount=" + juce::String(xruns)
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
    // Creates and shows a window hosting a piano roll for clip
    MainWindow(model::MidiClip& clip, engine::Transport& transport, double sampleRate)
        : DocumentWindow(
              "Howl",
              juce::Desktop::getInstance().getDefaultLookAndFeel()
                  .findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* pianoRoll = new ui::PianoRoll(clip, transport, sampleRate);
        setContentOwned(pianoRoll, true);
        setResizable(true, true);
        centreWithSize(900, 600);
        setVisible(true);
        pianoRoll->grabKeyboardFocus();
    }

    // Requests app shutdown when the window's close button is clicked
    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class HowlApp : public juce::JUCEApplication {
public:
    // Returns the app's display name
    const juce::String getApplicationName() override
    {
        return "Howl";
    }

    // Returns the app's version string
    const juce::String getApplicationVersion() override
    {
        return "0.1.0";
    }

    // Opens the piano roll, starts the audio device driving the sequencer, and starts the xrun watcher
    void initialise(const juce::String&) override
    {
        if (!m_audioDevice.open()) {
            juce::Logger::writeToLog("Howl: failed to open audio device");
            return;
        }

        const double sampleRate = m_audioDevice.getSampleRate();
        const int bufferSize = m_audioDevice.getBufferSize();

        m_synth.prepare(sampleRate, bufferSize);

        auto sequencer = std::make_unique<model::SequencerNode>(m_transport, m_clip, m_synth);
        sequencer->prepare(sampleRate);
        m_graph.addNode(std::move(sequencer));
        m_graph.prepare();

        m_mainWindow = std::make_unique<MainWindow>(m_clip, m_transport, sampleRate);

        m_audioDevice.start([this](AudioBlock& block) {
            const SampleCount pos = m_transport.advance(block.numFrames);
            m_graph.process(block, pos);
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
    engine::Transport m_transport;
    model::MidiClip m_clip;
    dsp::SubtractiveSynth m_synth;
    io::AudioDevice m_audioDevice;
    engine::Graph m_graph;
    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<XrunWatcher> m_xrunWatcher;
};

} // namespace howl

START_JUCE_APPLICATION(howl::HowlApp)
