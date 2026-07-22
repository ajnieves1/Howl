// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: application entry point, main window, and arrangement wiring

#include "core/Types.h"
#include "dsp/BuiltInEffectFactory.h"
#include "dsp/OfflineStretcher.h"
#include "dsp/SamplerInstrument.h"
#include "dsp/SubtractiveSynth.h"
#include "engine/Graph.h"
#include "engine/Instrument.h"
#include "engine/Metronome.h"
#include "engine/Node.h"
#include "engine/Transport.h"
#include "io/AudioDevice.h"
#include "io/AudioFile.h"
#include "io/MidiInputHub.h"
#include "model/Arrangement.h"
#include "model/ArrangementNode.h"
#include "model/AudioClip.h"
#include "model/CommandStack.h"
#include "model/Commands.h"
#include "model/MidiClip.h"
#include "model/MidiFileImport.h"
#include "model/Note.h"
#include "model/OfflineRenderer.h"
#include "model/Pattern.h"
#include "model/PreviewPlayer.h"
#include "model/Session.h"
#include "model/TrackFreezer.h"
#include "plugins/PluginEffect.h"
#include "plugins/PluginHost.h"
#include "plugins/PluginInstrument.h"
#include "plugins/SandboxedPluginInstance.h"
#include "project/ProjectSerializer.h"
#include "ui/BrowserFileTypes.h"
#include "ui/HowlLookAndFeel.h"
#include "ui/MainComponent.h"
#include "ui/PluginWindow.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

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

// Runs a callback whenever the audio device manager's setup changes, so the new
// state can be persisted
class AudioDeviceStateSaver : public juce::ChangeListener {
public:
    // Stores the callback to run on each change
    explicit AudioDeviceStateSaver(std::function<void()> onChanged)
        : m_onChanged(std::move(onChanged))
    {
    }

    // Runs the stored callback
    void changeListenerCallback(juce::ChangeBroadcaster*) override {
        if (m_onChanged) {
            m_onChanged();
        }
    }

private:
    std::function<void()> m_onChanged;
};

// Host chrome for the audio device selector; closes itself and runs a callback
// when the user clicks its close button
class AudioSettingsWindow : public juce::DialogWindow {
public:
    // Stores the callback to run when the user clicks the close button
    explicit AudioSettingsWindow(std::function<void()> onCloseRequested)
        : DialogWindow("Audio Settings",
                        juce::Desktop::getInstance().getDefaultLookAndFeel()
                            .findColour(juce::ResizableWindow::backgroundColourId),
                        true)
        , m_onCloseRequested(std::move(onCloseRequested))
    {
        setUsingNativeTitleBar(true);
    }

    // Runs the stored close callback
    void closeButtonPressed() override {
        if (m_onCloseRequested) {
            m_onCloseRequested();
        }
    }

private:
    std::function<void()> m_onCloseRequested;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
};

// Fires a callback on a fixed interval, drives periodic autosave
class AutosaveTimer : public juce::Timer {
public:
    // Stores the callback to run on each tick and starts the timer
    explicit AutosaveTimer(std::function<void()> onTick)
        : m_onTick(std::move(onTick))
    {
        startTimer(kIntervalMs);
    }

private:
    static constexpr int kIntervalMs = 120000;

    // Runs the stored callback
    void timerCallback() override {
        if (m_onTick) {
            m_onTick();
        }
    }

    std::function<void()> m_onTick;
};

// One learned CC to effect parameter binding
struct MidiMapping {
    int cc;
    model::StripAddress stripAddress;
    std::size_t effectIndex;
    int paramIndex;
};

// The parameter armed to receive the next CC that comes in
struct MidiLearnTarget {
    model::StripAddress stripAddress;
    std::size_t effectIndex;
    int paramIndex;
};

// Names a strip kind for JSON, matching the instruments and effects kind string convention
juce::String stripKindToString(model::StripKind kind) {
    switch (kind) {
        case model::StripKind::Track:
            return "track";
        case model::StripKind::Bus:
            return "bus";
        case model::StripKind::Master:
        default:
            return "master";
    }
}

// Parses a strip kind string, defaulting to Master for anything not recognized
model::StripKind stripKindFromString(const juce::String& kindString) {
    if (kindString == "track") {
        return model::StripKind::Track;
    }
    if (kindString == "bus") {
        return model::StripKind::Bus;
    }
    return model::StripKind::Master;
}

// Drains the MIDI input hub's CC queue at 30 Hz, message thread only, then runs
// a second callback every tick regardless (the preview player's garbage collection rides
// this same timer rather than starting a dedicated one)
class MidiLearnTimer : public juce::Timer {
public:
    // Stores the hub to drain, the callback to run for each CC event, and the per-tick callback
    MidiLearnTimer(io::MidiInputHub& hub, std::function<void(const MidiEvent&)> onCcEvent,
                    std::function<void()> onTick)
        : m_hub(hub)
        , m_onCcEvent(std::move(onCcEvent))
        , m_onTick(std::move(onTick))
    {
        startTimerHz(30);
    }

    // Pops every pending CC event and runs the callback for each, then runs the tick callback
    void timerCallback() override {
        MidiEvent event {};
        while (m_hub.popCcEvent(event)) {
            m_onCcEvent(event);
        }

        if (m_onTick) {
            m_onTick();
        }
    }

private:
    io::MidiInputHub& m_hub;
    std::function<void(const MidiEvent&)> m_onCcEvent;
    std::function<void()> m_onTick;
};

class MainWindow : public juce::DocumentWindow {
public:
    // Creates and shows a window hosting the whole app shell (MainComponent)
    MainWindow(model::Arrangement& arrangement, engine::Transport& transport, model::CommandStack& commandStack,
               model::Mixer& mixer, model::Session& session, model::PatternBank& patterns,
               model::ArrangementNode& arrangementNode, engine::IEffectFactory& factory,
               plugins::IPluginHost* pluginHost, double sampleRate, int maxBlockSize,
               const juce::File& browserRoot)
        : DocumentWindow(
              "Howl",
              juce::Desktop::getInstance().getDefaultLookAndFeel()
                  .findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        m_mainComponent = new ui::MainComponent(arrangement, transport, commandStack, mixer, session, patterns,
            arrangementNode, factory, pluginHost, sampleRate, maxBlockSize, browserRoot);
        setContentOwned(m_mainComponent, true);
        setMenuBar(m_mainComponent);
        setResizable(true, true);
        centreWithSize(1000, 700);
        setVisible(true);
        m_mainComponent->grabKeyboardFocus();
    }

    // Requests app shutdown when the window's close button is clicked
    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // Returns the shell component, so the app can communicate callbacks and trigger refreshes
    ui::MainComponent* mainComponent() const {
        return m_mainComponent;
    }

    // Sets the title bar to "Howl", or "<project> - Howl" once a project has a name
    void setProjectTitle(const juce::String& fileName) {
        setName(fileName.isEmpty() ? juce::String("Howl") : fileName + " - Howl");
    }

private:
    ui::MainComponent* m_mainComponent = nullptr;

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
        return "1.0.0";
    }

    // Builds the arrangement, opens the app shell, starts the audio device, and starts the xrun watcher
    void initialise(const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&m_lookAndFeel);

        juce::PropertiesFile::Options settingsOptions;
        settingsOptions.applicationName = "Howl";
        settingsOptions.filenameSuffix = "xml";
        settingsOptions.folderName = "Howl";
        settingsOptions.osxLibrarySubFolder = "Application Support";
        m_settings = std::make_unique<juce::PropertiesFile>(settingsOptions);

        const std::unique_ptr<juce::XmlElement> audioDeviceState = m_settings->getXmlValue("audioDeviceState");
        if (!m_audioDevice.open(audioDeviceState.get())) {
            juce::Logger::writeToLog("Howl: failed to open audio device");
            return;
        }

        m_audioDeviceStateSaver = std::make_unique<AudioDeviceStateSaver>([this] {
            if (auto state = m_audioDevice.manager().createStateXml()) {
                m_settings->setValue("audioDeviceState", state.get());
                m_settings->saveIfNeeded();
            }
        });
        m_audioDevice.manager().addChangeListener(m_audioDeviceStateSaver.get());

        m_sampleRate = m_audioDevice.getSampleRate();
        m_bufferSize = m_audioDevice.getBufferSize();

        m_pluginHost.rescan();

        const std::size_t midiTrackIndex = m_arrangement.addTrack("Lead", model::TrackKind::Midi);
        m_session.addTrackColumn(); // keeps the session grid's columns aligned with arrangement tracks
        model::MidiClip clip;
        clip.setLengthTicks(model::kTicksPerQuarter * 16); // 4 bars, so it is a clickable block before any notes exist
        m_arrangement.addMidiClipPlacement(midiTrackIndex, model::MidiClipPlacement { 0, clip });

        auto arrangementNode = std::make_unique<model::ArrangementNode>(m_transport, m_arrangement);
        arrangementNode->setSession(&m_session);
        arrangementNode->setPatternBank(&m_patterns);
        arrangementNode->setLiveNoteQueue(&m_midiInputHub.noteQueue());
        arrangementNode->setPreviewPlayer(&m_previewPlayer);
        arrangementNode->prepare(m_sampleRate, m_bufferSize, 2);
        m_arrangementNode = arrangementNode.get();
        m_graph.addNode(std::move(arrangementNode));
        m_graph.prepare();

        m_arrangementNode->mixer().addBus("Bus 1");

        // Seeds the initial MIDI track's instrument through the same bookkeeping every later add uses
        reconcileTrackInstruments();
        applyTrackInstruments();

        const juce::File browserRoot(m_settings->getValue("browserRoot"));
        m_mainWindow = std::make_unique<MainWindow>(m_arrangement, m_transport, m_commandStack,
            m_arrangementNode->mixer(), m_session, m_patterns, *m_arrangementNode, m_effectFactory, &m_pluginHost,
            m_sampleRate, m_bufferSize, browserRoot);

        ui::MainComponent* mainComponent = m_mainWindow->mainComponent();
        mainComponent->setBrowserWidth(m_settings->getIntValue("browserWidth", 220));

        const bool metronomeOn = m_settings->getBoolValue("metronomeEnabled", false);
        const bool countInOn = m_settings->getBoolValue("countInEnabled", false);
        m_metronomeEnabled.store(metronomeOn, std::memory_order_relaxed);
        m_countInEnabled.store(countInOn, std::memory_order_relaxed);
        mainComponent->setMetronomeEnabled(metronomeOn);
        mainComponent->setCountInEnabled(countInOn);
        mainComponent->onMetronomeToggled = [this](bool enabled) {
            m_metronomeEnabled.store(enabled, std::memory_order_relaxed);
            m_settings->setValue("metronomeEnabled", enabled);
            m_settings->saveIfNeeded();
        };
        mainComponent->onCountInToggled = [this](bool enabled) {
            m_countInEnabled.store(enabled, std::memory_order_relaxed);
            m_settings->setValue("countInEnabled", enabled);
            m_settings->saveIfNeeded();
        };

        mainComponent->onTracksChanged = [this] {
            reconcileTrackInstruments();
            rebuildAudioGraph();
            m_mainWindow->mainComponent()->refreshAllViews();
        };
        mainComponent->onCloneInstrumentRequested = [this](std::size_t source, std::size_t dest) {
            cloneInstrument(source, dest);
        };
        mainComponent->onInstrumentPickRequested = [this](std::size_t trackIndex) {
            showInstrumentPicker(trackIndex);
        };
        mainComponent->onInstrumentEditRequested = [this](std::size_t trackIndex) {
            openInstrumentEditor(trackIndex);
        };
        mainComponent->instrumentNameFor = [this](std::size_t trackIndex) -> juce::String {
            if (trackIndex < m_instrumentNames.size()) {
                return m_instrumentNames[trackIndex];
            }
            return {};
        };
        mainComponent->parameterNamesFor = [this](std::size_t trackIndex) -> std::vector<juce::String> {
            if (trackIndex >= m_trackInstruments.size() || m_trackInstruments[trackIndex] == nullptr) {
                return {};
            }

            engine::Instrument* instrument = m_trackInstruments[trackIndex].get();
            std::vector<juce::String> names;
            names.reserve(static_cast<std::size_t>(instrument->numParameters()));
            for (int i = 0; i < instrument->numParameters(); ++i) {
                names.emplace_back(instrument->parameterName(i));
            }
            return names;
        };
        mainComponent->onImportAudioRequested = [this] {
            showImportAudioFileChooser();
        };
        mainComponent->onBrowserRootChanged = [this](juce::File root) {
            m_settings->setValue("browserRoot", root.getFullPathName());
            m_settings->saveIfNeeded();
        };
        mainComponent->onBrowserWidthChanged = [this](int width) {
            m_settings->setValue("browserWidth", width);
            m_settings->saveIfNeeded();
        };
        mainComponent->onBrowserFileClicked = [this](juce::File file) {
            if (ui::filetypes::isAudioFile(file)) {
                startSamplePreview(file);
            }
        };
        mainComponent->onAudioFileDropped = [this](juce::String path, std::size_t trackIndex, int64_t tick) {
            std::size_t targetTrack = trackIndex;
            if (trackIndex >= m_arrangement.numTracks()
                || m_arrangement.track(trackIndex).kind != model::TrackKind::Audio) {
                targetTrack = ensureFirstAudioTrack();
            }
            importAudioFile(juce::File(path), targetTrack, tick);
        };
        mainComponent->onMidiFileDropped = [this](juce::String path, std::size_t trackIndex, int64_t tick) {
            importMidiFile(juce::File(path), trackIndex, tick);
        };
        mainComponent->onSampleAssignRequested = [this](std::size_t trackIndex, juce::File file) {
            assignSampleToTrack(trackIndex, file);
        };
        mainComponent->onPatchDropRequested = [this](std::size_t trackIndex, juce::File file) {
            loadPatchOntoTrack(trackIndex, file);
        };
        mainComponent->onStepPreviewRequested = [this](std::size_t trackIndex) {
            if (m_transport.isPlaying()) {
                return;
            }

            m_arrangementNode->setLiveTargetTrack(static_cast<std::ptrdiff_t>(trackIndex));
            m_midiInputHub.pushNoteEvent(MidiEvent { MidiEvent::Type::NoteOn, 60, 1.0f });
            m_midiInputHub.pushNoteEvent(MidiEvent { MidiEvent::Type::NoteOff, 60, 0.0f });
        };
        mainComponent->onNewProjectRequested = [this] {
            confirmDiscardIfDirty([this] { newProject(); });
        };
        mainComponent->onOpenProjectRequested = [this] {
            confirmDiscardIfDirty([this] { showOpenProjectFileChooser(); });
        };
        mainComponent->onSaveProjectRequested = [this] {
            saveCurrentProject();
        };
        mainComponent->onSaveAsProjectRequested = [this] {
            showSaveProjectFileChooser();
        };
        mainComponent->onExportAudioRequested = [this] {
            exportAudio();
        };
        mainComponent->recentProjectFiles = [this] {
            return recentFiles();
        };
        mainComponent->onOpenRecentRequested = [this](juce::String path) {
            confirmDiscardIfDirty([this, path] { openProjectFile(juce::File(path)); });
        };
        mainComponent->onAudioSettingsRequested = [this] {
            showAudioSettingsDialog();
        };
        mainComponent->onRewarpRequested = [this] {
            rewarpAllClips();
        };
        mainComponent->isTrackFrozen = [this](std::size_t trackIndex) {
            return m_arrangementNode->isFrozen(trackIndex);
        };
        mainComponent->onFreezeRequested = [this](std::size_t trackIndex, bool freeze) {
            if (freeze) {
                freezeTrack(trackIndex);
            } else {
                unfreezeTrack(trackIndex);
            }
        };
        mainComponent->isInstrumentCrashed = [this](std::size_t trackIndex) {
            auto* sandboxed = findSandboxedPluginInstrument(trackIndex);
            return sandboxed != nullptr && sandboxed->hasCrashed();
        };
        mainComponent->onRestartInstrumentRequested = [this](std::size_t trackIndex) {
            auto* sandboxed = findSandboxedPluginInstrument(trackIndex);
            if (sandboxed != nullptr) {
                sandboxed->restart();
            }
        };
        mainComponent->onTrackSelected = [this](std::ptrdiff_t trackIndex) {
            m_arrangementNode->setLiveTargetTrack(trackIndex);
        };
        mainComponent->onMidiLearnRequested = [this](model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex) {
            m_midiLearnTarget = MidiLearnTarget { stripAddress, effectIndex, paramIndex };
        };
        mainComponent->onMidiUnlearnRequested = [this](model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex) {
            removeMidiMappingFor(stripAddress, effectIndex, paramIndex);
        };
        mainComponent->isParameterMapped = [this](model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex) {
            return findMidiMappingFor(stripAddress, effectIndex, paramIndex) != nullptr;
        };
        mainComponent->isSandboxEnabled = [this] {
            return m_sandboxEnabled;
        };
        mainComponent->onSandboxToggled = [this](bool enabled) {
            m_sandboxEnabled = enabled;
            m_pluginHost.setSandboxed(enabled);
        };
        mainComponent->isPluginCrashed = [this](model::StripAddress stripAddress, std::size_t effectIndex) {
            auto* sandboxed = findSandboxedPluginEffect(stripAddress, effectIndex);
            return sandboxed != nullptr && sandboxed->hasCrashed();
        };
        mainComponent->onRestartPluginRequested = [this](model::StripAddress stripAddress, std::size_t effectIndex) {
            auto* sandboxed = findSandboxedPluginEffect(stripAddress, effectIndex);
            if (sandboxed != nullptr) {
                sandboxed->restart();
            }
        };
        // Re-renders the initial track's instrument label, now that instrumentNameFor is wired
        mainComponent->refreshAllViews();
        m_mainWindow->setProjectTitle({});

        startAudioDevice();

        m_xrunWatcher = std::make_unique<XrunWatcher>(m_audioDevice);
        m_xrunWatcher->start();

        m_midiLearnTimer = std::make_unique<MidiLearnTimer>(m_midiInputHub, [this](const MidiEvent& event) {
            handleMidiCcEvent(event);
        }, [this] {
            m_previewPlayer.collectGarbage();
        });

        m_autosaveTimer = std::make_unique<AutosaveTimer>([this] { autosaveTick(); });
    }

    // Stops the xrun watcher, closes the audio device, and closes the window
    void shutdown() override
    {
        m_audioSettingsWindow.reset();
        m_autosaveTimer.reset();
        m_midiLearnTimer.reset();
        m_xrunWatcher.reset();
        if (m_audioDeviceStateSaver != nullptr) {
            m_audioDevice.manager().removeChangeListener(m_audioDeviceStateSaver.get());
        }
        m_audioDevice.close();
        m_mainWindow.reset();
        if (m_settings != nullptr) {
            m_settings->saveIfNeeded();
        }
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    // Quits the app when the OS or window asks it to, guarding unsaved changes first
    void systemRequestedQuit() override
    {
        confirmDiscardIfDirty([this] { quit(); });
    }

private:
    // Starts the device on the one audio callback, used by every place that resumes the device
    void startAudioDevice()
    {
        m_audioDevice.start([this](AudioBlock& block) {
            audioCallback(block);
        });
    }

    // [RT] Runs the count in pre-roll if one is pending, then advances the transport, processes
    // the graph, and mixes the metronome click when it is on
    void audioCallback(AudioBlock& block)
    {
        const bool nowPlaying = m_transport.isPlaying();
        if (nowPlaying && !m_wasPlaying) {
            m_metronome.reset();
            m_countingIn = m_countInEnabled.load(std::memory_order_relaxed);
            m_countInPos = 0;
        }
        if (!nowPlaying) {
            m_countingIn = false;
        }
        m_wasPlaying = nowPlaying;

        const double tempo = m_transport.tempo();

        if (m_countingIn) {
            // Freeze the arrangement for one bar and play only the count in clicks
            for (int channel = 0; channel < block.numChannels; ++channel) {
                for (int frame = 0; frame < block.numFrames; ++frame) {
                    block.channels[channel][frame] = 0.0f;
                }
            }
            m_metronome.process(block, m_countInPos, block.numFrames, tempo, m_sampleRate);
            m_countInPos += static_cast<SampleCount>(block.numFrames);

            const double samplesPerBar = 4.0 * (60.0 / tempo) * m_sampleRate;
            if (static_cast<double>(m_countInPos) >= samplesPerBar) {
                m_countingIn = false;
                m_metronome.reset();
            }
            return;
        }

        const SampleCount pos = m_transport.advance(block.numFrames);
        m_graph.process(block, pos);
        if (nowPlaying && m_metronomeEnabled.load(std::memory_order_relaxed)) {
            m_metronome.process(block, pos, block.numFrames, tempo, m_sampleRate);
        }
    }

    // Pauses the device, re-prepares the arrangement node (which re-prepares the mixer in
    // place, preserving gain/pan/routing/sends), re-applies every track's instrument, resumes
    void rebuildAudioGraph()
    {
        // Any open instrument editor may hold a dangling reference once instruments are reassigned
        m_instrumentEditorWindow.reset();

        m_audioDevice.stop();
        m_arrangementNode->prepare(m_sampleRate, m_bufferSize, 2);
        applyTrackInstruments();
        startAudioDevice();
    }

    // Assigns every track's current instrument pointer onto the arrangement node
    void applyTrackInstruments()
    {
        for (std::size_t i = 0; i < m_trackInstruments.size(); ++i) {
            if (m_trackInstruments[i] != nullptr) {
                m_arrangementNode->setInstrumentForTrack(i, m_trackInstruments[i].get());
            }
        }
    }

    // Keeps the instrument vectors sized to the arrangement, seeding new MIDI tracks with a
    // fresh default synth. Known v1 limitation: removing a track that isn't the last one
    // shifts every later track's slot down by one, which can reset those tracks back to the
    // default synth instead of preserving a previously picked instrument (matches the
    // already-accepted "RemoveTrack undo restores a default strip" limitation).
    void reconcileTrackInstruments()
    {
        const std::size_t numTracks = m_arrangement.numTracks();
        m_trackInstruments.resize(numTracks);
        m_instrumentNames.resize(numTracks);

        for (std::size_t i = 0; i < numTracks; ++i) {
            if (m_arrangement.track(i).kind == model::TrackKind::Midi && m_trackInstruments[i] == nullptr) {
                auto synth = std::make_unique<dsp::SubtractiveSynth>();
                synth->prepare(m_sampleRate, m_bufferSize);
                m_trackInstruments[i] = std::move(synth);
                m_instrumentNames[i] = "Subtractive Synth";
            }
        }
    }

    // Opens a picker of the built in synth plus every hosted instrument plugin, assigns the pick
    void showInstrumentPicker(std::size_t trackIndex)
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Subtractive Synth");
        menu.addItem(2, "Sampler");

        std::vector<plugins::PluginDescriptor> instrumentPlugins;
        for (const auto& descriptor : m_pluginHost.list()) {
            if (descriptor.isInstrument) {
                instrumentPlugins.push_back(descriptor);
            }
        }

        for (std::size_t i = 0; i < instrumentPlugins.size(); ++i) {
            // The same plugin can appear once per format (VST3 and CLAP unified in one
            // picker), the format label is the only thing that tells those two apart
            const juce::String label = juce::String(instrumentPlugins[i].name) + " (" + instrumentPlugins[i].format + ")";
            menu.addItem(static_cast<int>(i + 3), label);
        }

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, trackIndex, instrumentPlugins](int result) {
            if (result <= 0) {
                return;
            }

            std::unique_ptr<engine::Instrument> instrument;
            juce::String name;

            if (result == 1) {
                instrument = std::make_unique<dsp::SubtractiveSynth>();
                name = "Subtractive Synth";
            } else if (result == 2) {
                // No sample assigned yet, silent until the channel rack assigns one
                instrument = std::make_unique<dsp::SamplerInstrument>();
                name = "Sampler";
            } else {
                const std::size_t pluginIndex = static_cast<std::size_t>(result - 3);
                if (pluginIndex < instrumentPlugins.size()) {
                    auto pluginInstance = m_pluginHost.instantiate(instrumentPlugins[pluginIndex]);
                    if (pluginInstance != nullptr) {
                        name = instrumentPlugins[pluginIndex].name;
                        instrument = std::make_unique<plugins::PluginInstrument>(
                            std::move(pluginInstance), instrumentPlugins[pluginIndex]);
                    }
                }
            }

            if (instrument == nullptr) {
                return;
            }

            instrument->prepare(m_sampleRate, m_bufferSize);

            if (trackIndex >= m_trackInstruments.size()) {
                m_trackInstruments.resize(trackIndex + 1);
                m_instrumentNames.resize(trackIndex + 1);
            }

            m_trackInstruments[trackIndex] = std::move(instrument);
            m_instrumentNames[trackIndex] = name;

            rebuildAudioGraph();
            m_mainWindow->mainComponent()->refreshAllViews();
        });
    }

    // Opens the native editor for a plugin instrument assigned to trackIndex, no-op for the
    // built-in synth or a plugin that reports no editor
    void openInstrumentEditor(std::size_t trackIndex)
    {
        if (trackIndex >= m_trackInstruments.size()) {
            return;
        }

        auto* pluginInstrument = dynamic_cast<plugins::PluginInstrument*>(m_trackInstruments[trackIndex].get());
        if (pluginInstrument == nullptr || !pluginInstrument->instance().hasEditor()) {
            return;
        }

        m_instrumentEditorWindow = std::make_unique<ui::PluginWindow>(
            pluginInstrument->instance(), pluginInstrument->displayName());
        m_instrumentEditorWindow->open();
    }

    // True when a mapping's strip and effect still resolve, false after a structural edit removes them
    bool midiMappingResolves(const MidiMapping& mapping) const
    {
        model::Mixer& mixer = m_arrangementNode->mixer();

        switch (mapping.stripAddress.kind) {
            case model::StripKind::Track:
                if (mapping.stripAddress.index >= m_arrangement.numTracks()) {
                    return false;
                }
                break;
            case model::StripKind::Bus:
                if (mapping.stripAddress.index >= mixer.numBuses()) {
                    return false;
                }
                break;
            case model::StripKind::Master:
            default:
                break;
        }

        return mapping.effectIndex < mixer.strip(mapping.stripAddress).effects().size();
    }

    // Returns the sandboxed proxy behind a strip's effect slot, or nullptr when the slot
    // does not resolve, is not a plugin effect, or is not running sandboxed at all
    plugins::SandboxedPluginInstance* findSandboxedPluginEffect(model::StripAddress stripAddress, std::size_t effectIndex)
    {
        model::Mixer& mixer = m_arrangementNode->mixer();

        switch (stripAddress.kind) {
            case model::StripKind::Track:
                if (stripAddress.index >= m_arrangement.numTracks()) {
                    return nullptr;
                }
                break;
            case model::StripKind::Bus:
                if (stripAddress.index >= mixer.numBuses()) {
                    return nullptr;
                }
                break;
            case model::StripKind::Master:
            default:
                break;
        }

        engine::EffectChain& chain = mixer.strip(stripAddress).effects();
        if (effectIndex >= chain.size()) {
            return nullptr;
        }

        auto* pluginEffect = dynamic_cast<plugins::PluginEffect*>(&chain.at(effectIndex));
        if (pluginEffect == nullptr) {
            return nullptr;
        }

        return dynamic_cast<plugins::SandboxedPluginInstance*>(&pluginEffect->instance());
    }

    // Returns the sandboxed proxy behind a track's instrument, or nullptr when the track does
    // not resolve, has no plugin instrument assigned, or is not running sandboxed at all
    plugins::SandboxedPluginInstance* findSandboxedPluginInstrument(std::size_t trackIndex)
    {
        if (trackIndex >= m_trackInstruments.size()) {
            return nullptr;
        }

        auto* pluginInstrument = dynamic_cast<plugins::PluginInstrument*>(m_trackInstruments[trackIndex].get());
        if (pluginInstrument == nullptr) {
            return nullptr;
        }

        return dynamic_cast<plugins::SandboxedPluginInstance*>(&pluginInstrument->instance());
    }

    // Returns the mapping bound to the given parameter, or nullptr when none is bound
    MidiMapping* findMidiMappingFor(model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex)
    {
        for (auto& mapping : m_midiMappings) {
            if (mapping.stripAddress.kind == stripAddress.kind && mapping.stripAddress.index == stripAddress.index
                && mapping.effectIndex == effectIndex && mapping.paramIndex == paramIndex) {
                return &mapping;
            }
        }
        return nullptr;
    }

    // Removes the mapping bound to the given parameter, if any
    void removeMidiMappingFor(model::StripAddress stripAddress, std::size_t effectIndex, int paramIndex)
    {
        m_midiMappings.erase(
            std::remove_if(m_midiMappings.begin(), m_midiMappings.end(),
                [&](const MidiMapping& mapping) {
                    return mapping.stripAddress.kind == stripAddress.kind
                        && mapping.stripAddress.index == stripAddress.index
                        && mapping.effectIndex == effectIndex && mapping.paramIndex == paramIndex;
                }),
            m_midiMappings.end());
    }

    // Binds the armed learn target to the first CC that comes in, or applies a bound CC's value
    void handleMidiCcEvent(const MidiEvent& event)
    {
        if (m_midiLearnTarget) {
            const MidiLearnTarget target = *m_midiLearnTarget;

            // Replace any existing mapping for this CC, and any existing mapping for this target
            m_midiMappings.erase(
                std::remove_if(m_midiMappings.begin(), m_midiMappings.end(),
                    [&](const MidiMapping& mapping) {
                        return mapping.cc == event.number
                            || (mapping.stripAddress.kind == target.stripAddress.kind
                                && mapping.stripAddress.index == target.stripAddress.index
                                && mapping.effectIndex == target.effectIndex
                                && mapping.paramIndex == target.paramIndex);
                    }),
                m_midiMappings.end());

            m_midiMappings.push_back(
                MidiMapping { event.number, target.stripAddress, target.effectIndex, target.paramIndex });
            m_midiLearnTarget.reset();
            return;
        }

        for (const auto& mapping : m_midiMappings) {
            if (mapping.cc != event.number || !midiMappingResolves(mapping)) {
                continue;
            }

            model::Mixer& mixer = m_arrangementNode->mixer();
            mixer.strip(mapping.stripAddress).effects().at(mapping.effectIndex).setParameter(mapping.paramIndex, event.value);
        }
    }

    // Returns the index of the first audio track, creating one (always at the current end,
    // since addTrack always appends) if the arrangement has none yet
    std::size_t ensureFirstAudioTrack()
    {
        for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
            if (m_arrangement.track(i).kind == model::TrackKind::Audio) {
                return i;
            }
        }

        const std::size_t newIndex = m_arrangement.numTracks();
        m_commandStack.perform(std::make_unique<model::AddTrackCommand>(
            m_arrangement, m_arrangementNode->mixer(), m_session, m_patterns, "Audio 1", model::TrackKind::Audio));
        reconcileTrackInstruments();
        return newIndex;
    }

    // Returns the index of the first MIDI track, creating one (always at the current end,
    // since addTrack always appends) if the arrangement has none yet
    std::size_t ensureFirstMidiTrack()
    {
        for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
            if (m_arrangement.track(i).kind == model::TrackKind::Midi) {
                return i;
            }
        }

        const std::size_t newIndex = m_arrangement.numTracks();
        m_commandStack.perform(std::make_unique<model::AddTrackCommand>(
            m_arrangement, m_arrangementNode->mixer(), m_session, m_patterns, "Lead", model::TrackKind::Midi));
        reconcileTrackInstruments();
        return newIndex;
    }

    // Reads a MIDI file into one clip, places it on a MIDI track, and rebuilds the graph/views
    void importMidiFile(const juce::File& file, std::size_t trackIndex, int64_t startTick)
    {
        std::optional<model::MidiClip> clip = model::importMidiFileAsClip(file);
        if (!clip.has_value()) {
            juce::Logger::writeToLog("Howl: failed to import MIDI file " + file.getFullPathName());
            return;
        }

        std::size_t targetTrack = trackIndex;
        if (trackIndex >= m_arrangement.numTracks()
            || m_arrangement.track(trackIndex).kind != model::TrackKind::Midi) {
            targetTrack = ensureFirstMidiTrack();
        }

        m_commandStack.perform(std::make_unique<model::AddMidiClipCommand>(
            m_arrangement, targetTrack, model::MidiClipPlacement { startTick, std::move(clip.value()) }));

        reconcileTrackInstruments();
        rebuildAudioGraph();
        m_mainWindow->mainComponent()->refreshAllViews();
    }

    // Copies the source channel's instrument kind and sample onto the cloned channel; a
    // built in synth clone keeps the default the reconcile pass already assigned, a hosted
    // plugin is not deep cloned and keeps that default
    void cloneInstrument(std::size_t source, std::size_t dest)
    {
        if (source >= m_trackInstruments.size() || m_trackInstruments[source] == nullptr) {
            return;
        }

        engine::Instrument* sourceInstrument = m_trackInstruments[source].get();
        if (auto* sampler = dynamic_cast<dsp::SamplerInstrument*>(sourceInstrument)) {
            const juce::File sampleFile(juce::String(sampler->sourcePath()));
            if (sampleFile.existsAsFile()) {
                assignSampleToTrack(dest, sampleFile);
            }
        }
    }

    // Loads a preset file into the plugin on trackIndex, stopping the device around the load so
    // the audio thread is never inside the instance while it changes. Tells the user when the
    // channel has no plugin or the plugin can not take that preset
    void loadPatchOntoTrack(std::size_t trackIndex, const juce::File& file)
    {
        plugins::PluginInstrument* pluginInstrument = trackIndex < m_trackInstruments.size()
            ? dynamic_cast<plugins::PluginInstrument*>(m_trackInstruments[trackIndex].get())
            : nullptr;

        if (pluginInstrument == nullptr) {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Preset",
                "This channel has no plugin instrument to load a preset into.");
            return;
        }

        m_audioDevice.stop();
        const bool ok = pluginInstrument->instance().loadPresetFile(file);
        startAudioDevice();

        if (ok) {
            m_mainWindow->mainComponent()->refreshAllViews();
        } else {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Preset",
                file.getFileName() + " could not be loaded into this instrument. A native preset such "
                "as .serumpreset or .vital only loads when the synth is added as CLAP.");
        }
    }

    // Reads a WAV file fully into memory, places it as a clip, and rebuilds the graph/views
    void importAudioFile(const juce::File& file, std::size_t trackIndex, int64_t startTick)
    {
        io::AudioFileReader reader;
        if (!reader.open(file.getFullPathName().toStdString())) {
            juce::Logger::writeToLog("Howl: failed to import audio file " + file.getFullPathName());
            return;
        }

        const int numChannels = reader.numChannels();
        const auto lengthSamples = static_cast<int>(reader.lengthInSamples());

        std::vector<std::vector<float>> channelData(static_cast<std::size_t>(numChannels),
            std::vector<float>(static_cast<std::size_t>(lengthSamples), 0.0f));
        std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
        for (int channel = 0; channel < numChannels; ++channel) {
            channelPointers[static_cast<std::size_t>(channel)] = channelData[static_cast<std::size_t>(channel)].data();
        }

        AudioBlock block { channelPointers.data(), numChannels, lengthSamples };
        reader.read(block, 0);

        model::AudioClip audioClip(std::move(channelData), reader.sampleRate());
        audioClip.setSourcePath(file.getFullPathName().toStdString());
        audioClip.setOriginalBpm(m_transport.tempo()); // warp stays off until the user opts in

        m_commandStack.perform(std::make_unique<model::AddAudioClipCommand>(
            m_arrangement, trackIndex, model::AudioClipPlacement { startTick, std::move(audioClip) }));

        rebuildAudioGraph();
        m_mainWindow->mainComponent()->refreshAllViews();
    }

    // Reads a wav file fully into memory and posts it to the preview player, message thread only
    void startSamplePreview(const juce::File& file)
    {
        io::AudioFileReader reader;
        if (!reader.open(file.getFullPathName().toStdString())) {
            juce::Logger::writeToLog("Howl: failed to preview audio file " + file.getFullPathName());
            return;
        }

        const int numChannels = reader.numChannels();
        const auto lengthSamples = static_cast<int>(reader.lengthInSamples());

        auto preview = std::make_unique<model::PreviewBuffer>();
        preview->channels.assign(static_cast<std::size_t>(numChannels),
            std::vector<float>(static_cast<std::size_t>(lengthSamples), 0.0f));

        std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
        for (int channel = 0; channel < numChannels; ++channel) {
            channelPointers[static_cast<std::size_t>(channel)] = preview->channels[static_cast<std::size_t>(channel)].data();
        }

        AudioBlock block { channelPointers.data(), numChannels, lengthSamples };
        reader.read(block, 0);

        m_previewPlayer.post(std::move(preview));
    }

    // Re-reads one audio clip's source file in place, preserving sourcePath/originalBpm/
    // warpEnabled; a missing or empty sourcePath logs and leaves the clip as an empty
    // placeholder (used right after a project load, where placements only carry metadata)
    void reReadAudioClip(model::AudioClip& clip)
    {
        const std::string path = clip.sourcePath();
        if (path.empty()) {
            return;
        }

        io::AudioFileReader reader;
        if (!reader.open(path)) {
            juce::Logger::writeToLog("Howl: missing audio file on load: " + juce::String(path));
            return;
        }

        const int numChannels = reader.numChannels();
        const auto lengthSamples = static_cast<int>(reader.lengthInSamples());

        std::vector<std::vector<float>> channelData(static_cast<std::size_t>(numChannels),
            std::vector<float>(static_cast<std::size_t>(lengthSamples), 0.0f));
        std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
        for (int channel = 0; channel < numChannels; ++channel) {
            channelPointers[static_cast<std::size_t>(channel)] = channelData[static_cast<std::size_t>(channel)].data();
        }

        AudioBlock block { channelPointers.data(), numChannels, lengthSamples };
        reader.read(block, 0);

        model::AudioClip realClip(std::move(channelData), reader.sampleRate());
        realClip.setSourcePath(path);
        realClip.setOriginalBpm(clip.originalBpm());
        realClip.setWarpEnabled(clip.warpEnabled());
        clip = realClip;
    }

    // Rewarps one clip in place: stretches its source to match tempo when warp is enabled and
    // the ratio isn't ~1, otherwise drops any warped buffer so playback falls back to source
    void rewarpClip(model::AudioClip& clip, double tempo)
    {
        if (!clip.warpEnabled() || clip.originalBpm() <= 0.0 || std::abs(clip.originalBpm() - tempo) < 0.01) {
            clip.clearWarpedChannels();
            return;
        }

        if (std::abs(clip.warpedTempo() - tempo) < 0.01) {
            return; // already rendered for this tempo
        }

        const int numChannels = clip.numChannels();
        const auto length = static_cast<std::size_t>(clip.lengthSamples());
        if (numChannels <= 0 || length == 0) {
            return;
        }

        std::vector<std::vector<float>> sourceChannels(static_cast<std::size_t>(numChannels));
        for (int channel = 0; channel < numChannels; ++channel) {
            const float* data = clip.channelData(channel);
            sourceChannels[static_cast<std::size_t>(channel)].assign(data, data + length);
        }

        const double ratio = clip.originalBpm() / tempo;
        auto stretched = dsp::OfflineStretcher::stretch(sourceChannels, clip.sourceSampleRate(), ratio);
        if (!stretched.empty()) {
            clip.setWarpedChannels(std::move(stretched), tempo);
        }
    }

    // Pauses the device, rewarps every audio clip (arrangement and session) for the current
    // tempo, resumes, and refreshes the views
    void rewarpAllClips()
    {
        const double tempo = m_transport.tempo();
        m_audioDevice.stop();

        for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
            model::Track& track = m_arrangement.track(i);
            for (auto& placement : track.audioClips) {
                rewarpClip(placement.clip, tempo);
            }
        }

        for (std::size_t trackIndex = 0; trackIndex < m_session.numTracks(); ++trackIndex) {
            for (std::size_t scene = 0; scene < m_session.numScenes(); ++scene) {
                model::ClipSlot& slot = m_session.slot(trackIndex, scene);
                if (slot.content == model::SlotContent::Audio) {
                    rewarpClip(slot.audioClip, tempo);
                }
            }
        }

        startAudioDevice();

        m_mainWindow->mainComponent()->refreshAllViews();
    }

    // Pauses the device, offline-renders the track through its instrument and strip FX, installs
    // the render so playback reads it and the strip's FX chain is bypassed, resumes
    void freezeTrack(std::size_t trackIndex)
    {
        m_audioDevice.stop();

        engine::Instrument* instrument = trackIndex < m_trackInstruments.size()
            ? m_trackInstruments[trackIndex].get() : nullptr;

        auto rendered = model::TrackFreezer::renderTrack(m_arrangement, m_arrangementNode->mixer(), m_transport,
            trackIndex, instrument, m_sampleRate, m_bufferSize, 2);

        if (!rendered.empty()) {
            m_arrangementNode->setFrozen(trackIndex, std::move(rendered));
        }

        startAudioDevice();

        m_mainWindow->mainComponent()->refreshAllViews();
    }

    // Pauses the device, drops the track's frozen render so live rendering and its FX chain
    // resume, resumes
    void unfreezeTrack(std::size_t trackIndex)
    {
        m_audioDevice.stop();
        m_arrangementNode->clearFrozen(trackIndex);

        startAudioDevice();

        m_mainWindow->mainComponent()->refreshAllViews();
    }

    // Returns the whole-song last clip end in samples across every track, both clip kinds,
    // using activeLengthSamples() so warped audio clips report their warped length
    SampleCount lastClipEndAcrossArrangement()
    {
        const double tempo = m_transport.tempo();
        const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(model::kTicksPerQuarter);

        SampleCount lastEnd = 0;
        for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
            const model::Track& track = m_arrangement.track(i);

            for (const auto& placement : track.midiClips) {
                const int64_t endTick = placement.startTick + placement.clip.lengthTicks();
                const auto endSample = static_cast<SampleCount>(static_cast<double>(endTick) * samplesPerTick);
                lastEnd = endSample > lastEnd ? endSample : lastEnd;
            }

            for (const auto& placement : track.audioClips) {
                const auto startSample = static_cast<SampleCount>(static_cast<double>(placement.startTick) * samplesPerTick);
                const SampleCount endSample = startSample + placement.clip.activeLengthSamples();
                lastEnd = endSample > lastEnd ? endSample : lastEnd;
            }
        }

        return lastEnd;
    }

    // Bounces the whole arrangement to a WAV: an empty arrangement shows an info box, otherwise
    // an async save-file chooser followed by a synchronous, device-paused render of the last
    // clip end plus a one second tail, rounded up to whole blocks
    void exportAudio()
    {
        const SampleCount lastEnd = lastClipEndAcrossArrangement();
        if (lastEnd <= 0) {
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "Export Audio", "The arrangement has no clips to export.");
            return;
        }

        const auto tailSamples = static_cast<SampleCount>(m_sampleRate);
        const SampleCount rawLength = lastEnd + tailSamples;
        const SampleCount numBlocks = (rawLength + m_bufferSize - 1) / m_bufferSize;
        const SampleCount lengthSamples = numBlocks * m_bufferSize;

        auto chooser = std::make_shared<juce::FileChooser>("Export Audio", juce::File(), "*.wav");
        constexpr int flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;

        chooser->launchAsync(flags, [this, chooser, lengthSamples](const juce::FileChooser& fc) {
            const juce::File file = fc.getResult();
            if (file == juce::File()) {
                return;
            }

            m_audioDevice.stop();
            m_arrangementNode->resetSessionPlayback();

            const bool ok = model::OfflineRenderer::renderNodeToFile(*m_arrangementNode, m_transport, m_sampleRate,
                m_bufferSize, 2, lengthSamples, file.withFileExtension(".wav"));

            if (!ok) {
                juce::Logger::writeToLog("Howl: failed to export audio to " + file.getFullPathName());
            }

            startAudioDevice();
        });
    }

    // Opens an async file chooser filtered to .wav, imports onto the first audio track at tick 0
    void showImportAudioFileChooser()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Import Audio", juce::File(), "*.wav");
        constexpr int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc) {
            const juce::File file = fc.getResult();
            if (file == juce::File()) {
                return;
            }

            const std::size_t trackIndex = ensureFirstAudioTrack();
            importAudioFile(file, trackIndex, 0);
        });
    }

    // Builds the per-track instrument array save() embeds: null for audio tracks,
    // {"kind":"subtractive","params":[...]} or {"kind":"plugin","name","format","path","state"}
    juce::var buildInstrumentsVar()
    {
        juce::Array<juce::var> instruments;

        for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
            if (m_arrangement.track(i).kind != model::TrackKind::Midi
                || i >= m_trackInstruments.size() || m_trackInstruments[i] == nullptr) {
                instruments.add(juce::var());
                continue;
            }

            engine::Instrument* instrument = m_trackInstruments[i].get();

            if (auto* pluginInstrument = dynamic_cast<plugins::PluginInstrument*>(instrument)) {
                const plugins::StateBlob blob = pluginInstrument->instance().saveState();
                auto* obj = new juce::DynamicObject();
                obj->setProperty("kind", "plugin");
                obj->setProperty("name", juce::String(pluginInstrument->displayName()));
                obj->setProperty("format", juce::String(pluginInstrument->pluginFormat()));
                obj->setProperty("path", juce::String(pluginInstrument->pluginPath()));
                obj->setProperty("state", juce::Base64::toBase64(blob.data(), blob.size()));
                instruments.add(juce::var(obj));
                continue;
            }

            if (auto* samplerInstrument = dynamic_cast<dsp::SamplerInstrument*>(instrument)) {
                auto* obj = new juce::DynamicObject();
                obj->setProperty("kind", "sampler");
                obj->setProperty("path", juce::String(samplerInstrument->sourcePath()));
                obj->setProperty("level", static_cast<double>(samplerInstrument->getParameter(0)));
                instruments.add(juce::var(obj));
                continue;
            }

            auto* obj = new juce::DynamicObject();
            obj->setProperty("kind", "subtractive");
            juce::Array<juce::var> params;
            for (int p = 0; p < instrument->numParameters(); ++p) {
                params.add(static_cast<double>(instrument->getParameter(p)));
            }
            obj->setProperty("params", params);
            instruments.add(juce::var(obj));
        }

        return instruments;
    }

    // Builds the midiMappings array save() embeds: one entry per learned CC binding
    juce::var buildMidiMappingsVar()
    {
        juce::Array<juce::var> mappings;

        for (const auto& mapping : m_midiMappings) {
            auto* obj = new juce::DynamicObject();
            obj->setProperty("cc", mapping.cc);
            obj->setProperty("stripKind", stripKindToString(mapping.stripAddress.kind));
            obj->setProperty("stripIndex", static_cast<int>(mapping.stripAddress.index));
            obj->setProperty("effectIndex", static_cast<int>(mapping.effectIndex));
            obj->setProperty("paramIndex", mapping.paramIndex);
            mappings.add(juce::var(obj));
        }

        return mappings;
    }

    // Rebuilds m_midiMappings from a loaded midiMappings var, dropping entries whose strip
    // or effect no longer resolves
    void rebuildMidiMappingsFromVar(const juce::var& midiMappingsVar)
    {
        m_midiMappings.clear();

        if (const auto* mappingsArray = midiMappingsVar.getArray()) {
            for (const auto& entry : *mappingsArray) {
                const MidiMapping mapping {
                    static_cast<int>(entry.getProperty("cc", 0)),
                    model::StripAddress {
                        stripKindFromString(entry.getProperty("stripKind", juce::var()).toString()),
                        static_cast<std::size_t>(static_cast<int>(entry.getProperty("stripIndex", 0)))
                    },
                    static_cast<std::size_t>(static_cast<int>(entry.getProperty("effectIndex", 0))),
                    static_cast<int>(entry.getProperty("paramIndex", 0))
                };

                if (midiMappingResolves(mapping)) {
                    m_midiMappings.push_back(mapping);
                }
            }
        }
    }

    // Reads the recent project files list from settings, newest first
    juce::StringArray recentFiles() const
    {
        juce::StringArray files;
        files.addLines(m_settings->getValue("recentFiles"));
        files.removeEmptyStrings();
        return files;
    }

    // Moves file to the front of the recent files list (deduping), caps at
    // kMaxRecentFiles, and persists
    void addRecentFile(const juce::File& file)
    {
        juce::StringArray files = recentFiles();
        const juce::String path = file.getFullPathName();
        files.removeString(path);
        files.insert(0, path);
        while (files.size() > kMaxRecentFiles) {
            files.remove(files.size() - 1);
        }

        m_settings->setValue("recentFiles", files.joinIntoString("\n"));
        m_settings->saveIfNeeded();
    }

    // Opens the modeless audio settings dialog, or brings the existing one to front
    void showAudioSettingsDialog()
    {
        if (m_audioSettingsWindow != nullptr) {
            m_audioSettingsWindow->toFront(true);
            return;
        }

        m_audioSettingsWindow = std::make_unique<AudioSettingsWindow>([this] {
            m_audioSettingsWindow.reset();
        });

        // No input channels: the user does not record live audio into Howl
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            m_audioDevice.manager(), 0, 0, 2, 2, false, false, true, true);
        selector->setSize(500, 450);

        m_audioSettingsWindow->setContentOwned(selector.release(), true);
        m_audioSettingsWindow->setResizable(false, false);
        m_audioSettingsWindow->centreWithSize(m_audioSettingsWindow->getWidth(), m_audioSettingsWindow->getHeight());
        m_audioSettingsWindow->setVisible(true);
        m_audioSettingsWindow->toFront(true);
    }

    // Serializes the current session, shared by saveProject and autosaveTick
    juce::String buildProjectJson()
    {
        return project::ProjectSerializer::save(m_arrangement, m_arrangementNode->mixer(),
            m_session, m_patterns, buildInstrumentsVar(), m_transport.tempo(), buildMidiMappingsVar());
    }

    // The sibling .autosave file's path for a project file
    static juce::File autosaveFileFor(const juce::File& file)
    {
        return juce::File(file.getFullPathName() + ".autosave");
    }

    // True when there are changes since the last save (or load), by the P12 dirty rule
    bool isDirty() const
    {
        return m_commandStack.changeCounter() != m_lastSavedChangeCounter;
    }

    // Writes a sibling .autosave file, only for a project that has a path and is dirty
    void autosaveTick()
    {
        if (m_currentProjectFile == juce::File() || !isDirty()) {
            return;
        }

        autosaveFileFor(m_currentProjectFile).replaceWithText(buildProjectJson());
    }

    // Runs onProceed immediately if the project is clean; otherwise asks Save / Don't Save /
    // Cancel first. Save routes through the save-as chooser if there is no current path yet,
    // and only runs onProceed once that save actually succeeds; Cancel runs nothing
    void confirmDiscardIfDirty(std::function<void()> onProceed)
    {
        if (!isDirty()) {
            onProceed();
            return;
        }

        const auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Save changes?")
            .withMessage("This project has unsaved changes.")
            .withButton("Save")
            .withButton("Don't Save")
            .withButton("Cancel");

        juce::AlertWindow::showAsync(options, [this, onProceed](int result) {
            if (result == 1) {
                if (m_currentProjectFile == juce::File()) {
                    showSaveProjectFileChooser(onProceed);
                } else {
                    saveProject(m_currentProjectFile);
                    onProceed();
                }
            } else if (result == 2) {
                onProceed();
            }
        });
    }

    // Opens file, offering to recover a newer sibling .autosave first if one exists
    void openProjectFile(const juce::File& file)
    {
        const juce::File autosave = autosaveFileFor(file);
        if (!autosave.existsAsFile() || autosave.getLastModificationTime() <= file.getLastModificationTime()) {
            loadProjectFromJson(file.loadFileAsString(), file);
            return;
        }

        const auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Recover autosaved changes?")
            .withMessage("A more recent autosave of \"" + file.getFileName() + "\" was found.")
            .withButton("Restore")
            .withButton("Discard");

        juce::AlertWindow::showAsync(options, [this, file, autosave](int result) {
            if (result == 1) {
                // Files left in place: the restored state stays dirty until an explicit save
                loadProjectFromJson(autosave.loadFileAsString(), file, false);
            } else {
                autosave.deleteFile();
                loadProjectFromJson(file.loadFileAsString(), file);
            }
        });
    }

    // Serializes the current session and writes it to file
    void saveProject(const juce::File& file)
    {
        if (!file.replaceWithText(buildProjectJson())) {
            juce::Logger::writeToLog("Howl: failed to write project file " + file.getFullPathName());
            return;
        }

        m_currentProjectFile = file;
        addRecentFile(file);
        m_lastSavedChangeCounter = m_commandStack.changeCounter();
        autosaveFileFor(file).deleteFile();
        m_mainWindow->setProjectTitle(file.getFileNameWithoutExtension());
    }

    // Saves to the current file, or prompts for one if this session has never been saved
    void saveCurrentProject()
    {
        if (m_currentProjectFile == juce::File()) {
            showSaveProjectFileChooser();
        } else {
            saveProject(m_currentProjectFile);
        }
    }

    // Opens an async save dialog defaulting to *.howl; runs onSaved after a successful save
    void showSaveProjectFileChooser(std::function<void()> onSaved = {})
    {
        auto chooser = std::make_shared<juce::FileChooser>("Save Project", juce::File(), "*.howl");
        constexpr int flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;

        chooser->launchAsync(flags, [this, chooser, onSaved](const juce::FileChooser& fc) {
            const juce::File file = fc.getResult();
            if (file == juce::File()) {
                return;
            }

            saveProject(file.withFileExtension(".howl"));
            if (onSaved) {
                onSaved();
            }
        });
    }

    // Opens an async open dialog filtered to *.howl
    void showOpenProjectFileChooser()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Open Project", juce::File(), "*.howl");
        constexpr int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc) {
            const juce::File file = fc.getResult();
            if (file == juce::File()) {
                return;
            }

            openProjectFile(file);
        });
    }

    // Stops the transport and device, parses json into the live arrangement/mixer (mutated in
    // place, ArrangementNode owns the mixer and cannot be replaced), re-reads audio clips from
    // their sourcePath, rebuilds instruments, rebuilds the audio graph, and resumes. markClean
    // false is for autosave recovery: the content differs from what's on sourceFile's disk, so
    // dirty tracking must not consider it saved
    void loadProjectFromJson(const juce::String& json, const juce::File& sourceFile, bool markClean = true)
    {
        // Every instrument is about to be replaced, an open editor would dangle
        m_instrumentEditorWindow.reset();
        m_midiLearnTarget.reset();

        m_transport.stop();
        m_audioDevice.stop();
        m_commandStack.clear();

        juce::var instrumentsVar;
        double tempo = 120.0;
        juce::var midiMappingsVar;

        const bool ok = project::ProjectSerializer::load(json, m_arrangement, m_arrangementNode->mixer(),
            m_session, m_patterns, m_effectFactory, &m_pluginHost, instrumentsVar, tempo, midiMappingsVar);

        if (!ok) {
            juce::Logger::writeToLog("Howl: failed to parse project file");
            startAudioDevice();
            return;
        }

        m_transport.setTempo(tempo);
        rebuildMidiMappingsFromVar(midiMappingsVar);

        // Re-read every audio clip's source file now that placements only carry sourcePath,
        // both in the arrangement and in the session grid
        for (std::size_t i = 0; i < m_arrangement.numTracks(); ++i) {
            model::Track& track = m_arrangement.track(i);
            for (auto& placement : track.audioClips) {
                reReadAudioClip(placement.clip);
            }
        }

        for (std::size_t trackIndex = 0; trackIndex < m_session.numTracks(); ++trackIndex) {
            for (std::size_t scene = 0; scene < m_session.numScenes(); ++scene) {
                model::ClipSlot& slot = m_session.slot(trackIndex, scene);
                if (slot.content == model::SlotContent::Audio) {
                    reReadAudioClip(slot.audioClip);
                }
            }
        }

        rebuildInstrumentsFromVar(instrumentsVar);

        m_arrangementNode->prepare(m_sampleRate, m_bufferSize, 2);
        applyTrackInstruments();

        m_currentProjectFile = sourceFile;
        if (sourceFile != juce::File()) {
            addRecentFile(sourceFile);
        }
        m_lastSavedChangeCounter = markClean ? m_commandStack.changeCounter()
                                              : std::numeric_limits<std::uint64_t>::max();
        m_mainWindow->setProjectTitle(sourceFile == juce::File() ? juce::String() : sourceFile.getFileNameWithoutExtension());

        rewarpAllClips(); // stops/starts the device, rewarps every clip, refreshes all views
    }

    // Rebuilds m_trackInstruments/m_instrumentNames from a loaded instruments var, falling back
    // to a default synth for any MIDI track left without one (missing plugin, null entry, etc.)
    void rebuildInstrumentsFromVar(const juce::var& instrumentsVar)
    {
        m_trackInstruments.clear();
        m_instrumentNames.clear();
        m_trackInstruments.resize(m_arrangement.numTracks());
        m_instrumentNames.resize(m_arrangement.numTracks());

        if (const auto* instrumentsArray = instrumentsVar.getArray()) {
            for (int i = 0; i < instrumentsArray->size() && static_cast<std::size_t>(i) < m_trackInstruments.size(); ++i) {
                const juce::var& entry = (*instrumentsArray)[i];
                if (entry.isVoid() || entry.isUndefined()) {
                    continue;
                }

                const auto trackIndex = static_cast<std::size_t>(i);
                const juce::String kind = entry.getProperty("kind", juce::var()).toString();

                if (kind == "subtractive") {
                    auto synth = std::make_unique<dsp::SubtractiveSynth>();
                    synth->prepare(m_sampleRate, m_bufferSize);
                    if (const auto* paramsArray = entry.getProperty("params", juce::var()).getArray()) {
                        for (int p = 0; p < paramsArray->size() && p < synth->numParameters(); ++p) {
                            synth->setParameter(p, static_cast<float>(static_cast<double>((*paramsArray)[p])));
                        }
                    }
                    m_trackInstruments[trackIndex] = std::move(synth);
                    m_instrumentNames[trackIndex] = "Subtractive Synth";
                } else if (kind == "plugin") {
                    assignPluginInstrumentFromVar(entry, trackIndex);
                } else if (kind == "sampler") {
                    assignSamplerInstrumentFromVar(entry, trackIndex);
                }
            }
        }

        // Fills any MIDI track still lacking an instrument (audio tracks stay empty)
        reconcileTrackInstruments();
    }

    // Finds and instantiates a plugin instrument by name/format/path, restores its state,
    // falls back to a default synth (logged) when the plugin cannot be found or instantiated
    void assignPluginInstrumentFromVar(const juce::var& entry, std::size_t trackIndex)
    {
        const juce::String name = entry.getProperty("name", juce::var()).toString();
        const juce::String format = entry.getProperty("format", juce::var()).toString();
        const juce::String path = entry.getProperty("path", juce::var()).toString();

        plugins::PluginDescriptor found;
        bool foundAny = false;
        for (const auto& descriptor : m_pluginHost.list()) {
            if (descriptor.name == name.toStdString() && descriptor.format == format.toStdString()
                && descriptor.path == path.toStdString()) {
                found = descriptor;
                foundAny = true;
                break;
            }
        }

        std::unique_ptr<engine::Instrument> instrument;
        juce::String instrumentName;

        if (foundAny) {
            auto pluginInstance = m_pluginHost.instantiate(found);
            if (pluginInstance != nullptr) {
                // Base64::toBase64() (used to write "state" in buildInstrumentsVar) pairs
                // with Base64::convertFromBase64(), not MemoryBlock::fromBase64Encoding(),
                // which expects its own "byteCount.base64" format and otherwise just
                // silently fails to an empty block
                juce::MemoryOutputStream decodedState;
                juce::Base64::convertFromBase64(decodedState, entry.getProperty("state", juce::var()).toString());
                const auto* bytes = static_cast<const uint8_t*>(decodedState.getData());
                plugins::StateBlob blob(bytes, bytes + decodedState.getDataSize());
                pluginInstance->loadState(blob);
                instrument = std::make_unique<plugins::PluginInstrument>(std::move(pluginInstance), found);
                instrumentName = name;
            }
        }

        if (instrument == nullptr) {
            juce::Logger::writeToLog("Howl: plugin instrument '" + name + "' missing, using default synth");
            instrument = std::make_unique<dsp::SubtractiveSynth>();
            instrumentName = "Subtractive Synth";
        }

        instrument->prepare(m_sampleRate, m_bufferSize);
        m_trackInstruments[trackIndex] = std::move(instrument);
        m_instrumentNames[trackIndex] = instrumentName;
    }

    // Re-reads the wav at path into a SamplerInstrument, same read path as importAudioFile; a
    // missing file leaves it sample-less and silent, remembering the path for a future re-save
    void assignSamplerInstrumentFromVar(const juce::var& entry, std::size_t trackIndex)
    {
        const juce::String path = entry.getProperty("path", juce::var()).toString();
        const float level = static_cast<float>(static_cast<double>(entry.getProperty("level", 0.8)));

        auto sampler = std::make_unique<dsp::SamplerInstrument>();
        sampler->prepare(m_sampleRate, m_bufferSize);

        io::AudioFileReader reader;
        if (path.isNotEmpty() && reader.open(path.toStdString())) {
            const int numChannels = reader.numChannels();
            const auto lengthSamples = static_cast<int>(reader.lengthInSamples());

            std::vector<std::vector<float>> channelData(static_cast<std::size_t>(numChannels),
                std::vector<float>(static_cast<std::size_t>(lengthSamples), 0.0f));
            std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
            for (int channel = 0; channel < numChannels; ++channel) {
                channelPointers[static_cast<std::size_t>(channel)] = channelData[static_cast<std::size_t>(channel)].data();
            }

            AudioBlock block { channelPointers.data(), numChannels, lengthSamples };
            reader.read(block, 0);

            sampler->setSample(std::move(channelData), path.toStdString());
        } else {
            if (path.isNotEmpty()) {
                juce::Logger::writeToLog("Howl: sampler sample missing, staying silent: " + path);
            }
            sampler->setSample({}, path.toStdString());
        }

        sampler->setParameter(0, level);
        m_trackInstruments[trackIndex] = std::move(sampler);
        m_instrumentNames[trackIndex] = "Sampler";
    }

    // Reads file's wav data into a fresh SamplerInstrument and installs it on trackIndex,
    // device paused, the same tail the instrument picker uses for every other pick
    void assignSampleToTrack(std::size_t trackIndex, const juce::File& file)
    {
        io::AudioFileReader reader;
        if (!reader.open(file.getFullPathName().toStdString())) {
            juce::Logger::writeToLog("Howl: failed to load sample " + file.getFullPathName());
            return;
        }

        const int numChannels = reader.numChannels();
        const auto lengthSamples = static_cast<int>(reader.lengthInSamples());

        std::vector<std::vector<float>> channelData(static_cast<std::size_t>(numChannels),
            std::vector<float>(static_cast<std::size_t>(lengthSamples), 0.0f));
        std::vector<float*> channelPointers(static_cast<std::size_t>(numChannels));
        for (int channel = 0; channel < numChannels; ++channel) {
            channelPointers[static_cast<std::size_t>(channel)] = channelData[static_cast<std::size_t>(channel)].data();
        }

        AudioBlock block { channelPointers.data(), numChannels, lengthSamples };
        reader.read(block, 0);

        auto sampler = std::make_unique<dsp::SamplerInstrument>();
        sampler->prepare(m_sampleRate, m_bufferSize);
        sampler->setSample(std::move(channelData), file.getFullPathName().toStdString());

        if (trackIndex >= m_trackInstruments.size()) {
            m_trackInstruments.resize(trackIndex + 1);
            m_instrumentNames.resize(trackIndex + 1);
        }

        m_trackInstruments[trackIndex] = std::move(sampler);
        m_instrumentNames[trackIndex] = "Sampler";

        rebuildAudioGraph();
        m_mainWindow->mainComponent()->refreshAllViews();
    }

    // Loads the built-in default session: one MIDI track "Lead", one empty 4-bar clip, one
    // bus, 120 BPM. Same code path as opening a file, just with a literal JSON string
    void newProject()
    {
        const juce::String defaultJson = R"({
            "version": 1,
            "tempo": 120.0,
            "tracks": [
                { "name": "Lead", "kind": "midi",
                  "midiClips": [ { "startTick": 0, "lengthTicks": 15360, "notes": [] } ],
                  "audioClips": [],
                  "strip": { "gainDb": 0.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] },
                  "output": -1, "sends": [] }
            ],
            "buses": [
                { "name": "Bus 1", "strip": { "gainDb": 0.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] } }
            ],
            "masterStrip": { "gainDb": 0.0, "pan": 0.0, "muted": false, "soloed": false, "effects": [] },
            "instruments": [ { "kind": "subtractive", "params": [] } ]
        })";

        loadProjectFromJson(defaultJson, juce::File());
    }

    static constexpr int kMaxRecentFiles = 8;

    ui::HowlLookAndFeel m_lookAndFeel;
    engine::Transport m_transport;
    model::Arrangement m_arrangement;
    model::Session m_session;
    model::PatternBank m_patterns;
    model::CommandStack m_commandStack;
    dsp::BuiltInEffectFactory m_effectFactory;
    plugins::PluginHost m_pluginHost;
    bool m_sandboxEnabled = true;
    io::AudioDevice m_audioDevice;
    io::MidiInputHub m_midiInputHub;
    model::PreviewPlayer m_previewPlayer;
    std::unique_ptr<MidiLearnTimer> m_midiLearnTimer;
    std::vector<MidiMapping> m_midiMappings;
    std::optional<MidiLearnTarget> m_midiLearnTarget;
    engine::Graph m_graph;
    engine::Metronome m_metronome;
    model::ArrangementNode* m_arrangementNode = nullptr;
    double m_sampleRate = 44100.0;
    int m_bufferSize = 0;

    // Set from the UI thread, read on the audio thread
    std::atomic<bool> m_metronomeEnabled { false };
    std::atomic<bool> m_countInEnabled { false };

    // Audio thread only, drives the count in pre-roll
    bool m_wasPlaying = false;
    bool m_countingIn = false;
    SampleCount m_countInPos = 0;
    std::vector<std::unique_ptr<engine::Instrument>> m_trackInstruments;
    std::vector<juce::String> m_instrumentNames;
    juce::File m_currentProjectFile;
    std::uint64_t m_lastSavedChangeCounter = 0;
    std::unique_ptr<ui::PluginWindow> m_instrumentEditorWindow;
    std::unique_ptr<MainWindow> m_mainWindow;
    std::unique_ptr<XrunWatcher> m_xrunWatcher;
    std::unique_ptr<juce::PropertiesFile> m_settings;
    std::unique_ptr<AudioDeviceStateSaver> m_audioDeviceStateSaver;
    std::unique_ptr<juce::DialogWindow> m_audioSettingsWindow;
    std::unique_ptr<AutosaveTimer> m_autosaveTimer;
};

} // namespace howl

START_JUCE_APPLICATION(howl::HowlApp)
