// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: application entry point, main window, and arrangement wiring

#include "core/Types.h"
#include "dsp/BuiltInEffectFactory.h"
#include "dsp/SubtractiveSynth.h"
#include "engine/Graph.h"
#include "engine/Node.h"
#include "engine/Transport.h"
#include "io/AudioDevice.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"
#include "model/CommandStack.h"
#include "model/MidiClip.h"
#include "model/Note.h"
#include "plugins/PluginHost.h"
#include "ui/MainComponent.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

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
    // Creates and shows a window hosting the whole app shell (MainComponent)
    MainWindow(model::Arrangement& arrangement, engine::Transport& transport, model::CommandStack& commandStack,
               model::Mixer& mixer, engine::IEffectFactory& factory, plugins::IPluginHost* pluginHost,
               double sampleRate, int maxBlockSize)
        : DocumentWindow(
              "Howl",
              juce::Desktop::getInstance().getDefaultLookAndFeel()
                  .findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* mainComponent = new ui::MainComponent(arrangement, transport, commandStack, mixer,
            factory, pluginHost, sampleRate, maxBlockSize);
        setContentOwned(mainComponent, true);
        setResizable(true, true);
        centreWithSize(1000, 700);
        setVisible(true);
        mainComponent->grabKeyboardFocus();
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

    // Builds the arrangement, opens the arrange view, starts the audio device, and starts the xrun watcher
    void initialise(const juce::String&) override
    {
        if (!m_audioDevice.open()) {
            juce::Logger::writeToLog("Howl: failed to open audio device");
            return;
        }

        m_sampleRate = m_audioDevice.getSampleRate();
        m_bufferSize = m_audioDevice.getBufferSize();

        m_synth.prepare(m_sampleRate, m_bufferSize);
        m_pluginHost.rescan();

        const std::size_t midiTrackIndex = m_arrangement.addTrack("Lead", model::TrackKind::Midi);
        model::MidiClip clip;
        clip.setLengthTicks(model::kTicksPerQuarter * 16); // 4 bars, so it is a clickable block before any notes exist
        m_arrangement.addMidiClipPlacement(midiTrackIndex, model::MidiClipPlacement { 0, clip });

        auto arrangementNode = std::make_unique<model::ArrangementNode>(m_transport, m_arrangement);
        arrangementNode->prepare(m_sampleRate, m_bufferSize, 2);
        arrangementNode->setInstrumentForTrack(midiTrackIndex, &m_synth);
        m_arrangementNode = arrangementNode.get();
        m_graph.addNode(std::move(arrangementNode));
        m_graph.prepare();

        m_arrangementNode->mixer().addBus("Bus 1");

        m_mainWindow = std::make_unique<MainWindow>(m_arrangement, m_transport, m_commandStack,
            m_arrangementNode->mixer(), m_effectFactory, &m_pluginHost, m_sampleRate, m_bufferSize);

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
    model::Arrangement m_arrangement;
    model::CommandStack m_commandStack;
    dsp::SubtractiveSynth m_synth;
    dsp::BuiltInEffectFactory m_effectFactory;
    plugins::PluginHost m_pluginHost;
    io::AudioDevice m_audioDevice;
    engine::Graph m_graph;
    model::ArrangementNode* m_arrangementNode = nullptr;
    double m_sampleRate = 44100.0;
    int m_bufferSize = 0;
    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<XrunWatcher> m_xrunWatcher;
};

} // namespace howl

START_JUCE_APPLICATION(howl::HowlApp)
