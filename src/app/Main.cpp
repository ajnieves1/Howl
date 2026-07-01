// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: application entry point, main window, and arrangement wiring

#include "core/Types.h"
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
#include "ui/ArrangeView.h"
#include "ui/PianoRoll.h"

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
    // Creates and shows a window hosting the arrange view
    MainWindow(model::Arrangement& arrangement, engine::Transport& transport, model::CommandStack& commandStack,
               double sampleRate, std::function<void(std::size_t, std::size_t)> onMidiClipSelected)
        : DocumentWindow(
              "Howl",
              juce::Desktop::getInstance().getDefaultLookAndFeel()
                  .findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* arrangeView = new ui::ArrangeView(arrangement, transport, commandStack, sampleRate);
        arrangeView->onMidiClipSelected = std::move(onMidiClipSelected);
        setContentOwned(arrangeView, true);
        setResizable(true, true);
        centreWithSize(900, 600);
        setVisible(true);
        arrangeView->grabKeyboardFocus();
    }

    // Requests app shutdown when the window's close button is clicked
    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class PianoRollWindow : public juce::DocumentWindow {
public:
    // Creates and shows a window hosting a piano roll for clip
    PianoRollWindow(model::MidiClip& clip, engine::Transport& transport, double sampleRate)
        : DocumentWindow(
              "Howl: Piano Roll",
              juce::Desktop::getInstance().getDefaultLookAndFeel()
                  .findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        auto* pianoRoll = new ui::PianoRoll(clip, transport, sampleRate);
        setContentOwned(pianoRoll, true);
        setResizable(true, true);
        centreWithSize(900, 400);
        setVisible(true);
        pianoRoll->grabKeyboardFocus();
    }

    // Hides the window, does not quit the app, only the main window does that
    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollWindow)
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
        const int bufferSize = m_audioDevice.getBufferSize();

        m_synth.prepare(m_sampleRate, bufferSize);

        const std::size_t midiTrackIndex = m_arrangement.addTrack("Lead", model::TrackKind::Midi);
        model::MidiClip clip;
        clip.setLengthTicks(model::kTicksPerQuarter * 16); // 4 bars, so it is a clickable block before any notes exist
        m_arrangement.addMidiClipPlacement(midiTrackIndex, model::MidiClipPlacement { 0, clip });

        auto arrangementNode = std::make_unique<model::ArrangementNode>(m_transport, m_arrangement);
        arrangementNode->prepare(m_sampleRate, bufferSize, 2);
        arrangementNode->setInstrumentForTrack(midiTrackIndex, &m_synth);
        m_graph.addNode(std::move(arrangementNode));
        m_graph.prepare();

        m_mainWindow = std::make_unique<MainWindow>(m_arrangement, m_transport, m_commandStack, m_sampleRate,
            [this](std::size_t trackIndex, std::size_t placementIndex) {
                openPianoRollFor(trackIndex, placementIndex);
            });

        m_audioDevice.start([this](AudioBlock& block) {
            const SampleCount pos = m_transport.advance(block.numFrames);
            m_graph.process(block, pos);
        });

        m_xrunWatcher = std::make_unique<XrunWatcher>(m_audioDevice);
        m_xrunWatcher->start();
    }

    // Stops the xrun watcher, closes the audio device, and closes the windows
    void shutdown() override
    {
        m_xrunWatcher.reset();
        m_audioDevice.close();
        m_pianoRollWindow.reset();
        m_mainWindow.reset();
    }

    // Quits the app when the OS or window asks it to
    void systemRequestedQuit() override
    {
        quit();
    }

private:
    // Opens (or replaces) the piano roll window for the given MIDI clip placement
    void openPianoRollFor(std::size_t trackIndex, std::size_t placementIndex)
    {
        model::MidiClip& clip = m_arrangement.track(trackIndex).midiClips[placementIndex].clip;
        m_pianoRollWindow = std::make_unique<PianoRollWindow>(clip, m_transport, m_sampleRate);
    }

    engine::Transport m_transport;
    model::Arrangement m_arrangement;
    model::CommandStack m_commandStack;
    dsp::SubtractiveSynth m_synth;
    io::AudioDevice m_audioDevice;
    engine::Graph m_graph;
    double m_sampleRate = 44100.0;
    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<PianoRollWindow> m_pianoRollWindow;
    std::unique_ptr<XrunWatcher> m_xrunWatcher;
};

} // namespace howl

START_JUCE_APPLICATION(howl::HowlApp)
