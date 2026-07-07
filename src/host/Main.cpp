// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: sandboxed plugin host helper, child side of the shared memory audio protocol and
// the JSON control protocol (parameters, state, and the editor) over stdin/stdout

#include "plugins/ClapAdapter.h"
#include "plugins/IPluginInstance.h"
#include "plugins/ShmAudioChannel.h"
#include "plugins/Vst3Adapter.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using howl::AudioBlock;
using howl::MidiEvent;
using howl::plugins::ClapAdapter;
using howl::plugins::IPluginInstance;
using howl::plugins::ParamInfo;
using howl::plugins::ShmAudioChannel;
using howl::plugins::StateBlob;
using howl::plugins::Vst3Adapter;

namespace {

// Parsed command line: shared memory geometry, plus either a debug mode (loopback and/or
// a crash countdown) or a real plugin to load (format/path/name all set together)
struct HostArgs {
    juce::String shmPath;
    int numChannels = 2;
    int blockSize = 512;
    double sampleRate = 44100.0;
    bool loopback = false;

    // Number of blocks to process normally before std::abort(), negative means never
    int crashAfter = -1;

    juce::String format;
    juce::String path;
    juce::String name;
};

// Reads every recognized flag from argv, missing --shm is fatal
std::optional<HostArgs> parseArgs(int argc, char* argv[]) {
    HostArgs args;

    for (int i = 1; i < argc; ++i) {
        const juce::String arg(argv[i]);

        if (arg == "--shm" && i + 1 < argc) {
            args.shmPath = argv[++i];
        } else if (arg == "--channels" && i + 1 < argc) {
            args.numChannels = juce::String(argv[++i]).getIntValue();
        } else if (arg == "--block" && i + 1 < argc) {
            args.blockSize = juce::String(argv[++i]).getIntValue();
        } else if (arg == "--samplerate" && i + 1 < argc) {
            args.sampleRate = juce::String(argv[++i]).getDoubleValue();
        } else if (arg == "--loopback") {
            args.loopback = true;
        } else if (arg == "--crash-after" && i + 1 < argc) {
            args.crashAfter = juce::String(argv[++i]).getIntValue();
        } else if (arg == "--format" && i + 1 < argc) {
            args.format = argv[++i];
        } else if (arg == "--path" && i + 1 < argc) {
            args.path = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            args.name = argv[++i];
        }
    }

    if (args.shmPath.isEmpty()) {
        return std::nullopt;
    }

    return args;
}

// True when a full line is waiting on stdin right now, without blocking to find out
bool lineWaitingOnStdin() {
   #if JUCE_WINDOWS
    const HANDLE stdinHandle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD bytesAvailable = 0;
    return PeekNamedPipe(stdinHandle, nullptr, 0, nullptr, &bytesAvailable, nullptr) != 0 && bytesAvailable > 0;
   #else
    pollfd pfd { STDIN_FILENO, POLLIN, 0 };
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0;
   #endif
}

// Returns the next stdin line if a full one is already waiting, without blocking
std::optional<juce::String> tryReadStdinLine() {
    if (!lineWaitingOnStdin()) {
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }

    return juce::String(line);
}

// Loads the plugin named by args, nullptr on any failure. VST3 scans just the given file
// for a description matching name; CLAP resolves the same way to get the plugin's id
std::unique_ptr<IPluginInstance> loadRequestedPlugin(const HostArgs& args) {
    if (args.format == "CLAP") {
        for (const auto& info : ClapAdapter::scanFile(args.path.toStdString())) {
            if (juce::String(info.name) == args.name) {
                return ClapAdapter::load(info);
            }
        }
        return nullptr;
    }

    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    juce::OwnedArray<juce::PluginDescription> descriptions;
    for (auto* format : formatManager.getFormats()) {
        format->findAllTypesForFile(descriptions, args.path);
    }

    for (auto* description : descriptions) {
        if (description->name == args.name) {
            juce::String errorMessage;
            auto instance = formatManager.createPluginInstance(*description, args.sampleRate, args.blockSize, errorMessage);
            if (instance == nullptr) {
                return nullptr;
            }
            return std::make_unique<Vst3Adapter>(std::move(instance));
        }
    }

    return nullptr;
}

// Writes one JSON reply line to stdout, the parent's sendLineAndWaitForReply() reads it
void sendReply(const juce::var& value) {
    std::cout << juce::JSON::toString(value, true) << std::endl;
}

// Builds the getParams reply: one {id, name, defaultNormalized} object per parameter
juce::var buildParamsReply(const std::vector<ParamInfo>& params) {
    juce::Array<juce::var> array;
    for (const auto& param : params) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id", static_cast<int>(param.id));
        obj->setProperty("name", juce::String(param.name));
        obj->setProperty("defaultNormalized", static_cast<double>(param.defaultNormalized));
        array.add(juce::var(obj));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("params", array);
    return juce::var(root);
}

// The plugin's editor, hosted as a plain top level window owned entirely by this process.
// Never reparented into Howl, per the sandbox design; closing it just hides it
class ChildEditorWindow : public juce::DocumentWindow {
public:
    ChildEditorWindow(const juce::String& title, juce::Component& editor)
        : DocumentWindow(title,
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour(juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&editor, true);
        if (auto* processorEditor = dynamic_cast<juce::AudioProcessorEditor*>(&editor)) {
            setResizable(processorEditor->isResizable(), false);
        }
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override {
        setVisible(false);
    }
};

} // namespace

int main(int argc, char* argv[]) {
    const auto args = parseArgs(argc, argv);
    if (!args.has_value()) {
        std::fprintf(stderr, "Howl host: --shm <path> is required\n");
        return 1;
    }

    auto channel = ShmAudioChannel::open(juce::File(args->shmPath));
    if (channel == nullptr) {
        std::fprintf(stderr, "Howl host: failed to open shared memory at %s\n", args->shmPath.toRawUTF8());
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    std::unique_ptr<IPluginInstance> plugin;
    if (args->format.isNotEmpty()) {
        plugin = loadRequestedPlugin(*args);
        if (plugin == nullptr) {
            std::fprintf(stderr, "Howl host: failed to load '%s' from '%s'\n",
                         args->name.toRawUTF8(), args->path.toRawUTF8());
            return 1;
        }
        plugin->prepare(args->sampleRate, args->blockSize);
    }

    std::mutex pluginMutex;
    std::unique_ptr<ChildEditorWindow> editorWindow;
    std::atomic<bool> quitRequested { false };

    // The audio thread: waits for each block, feeds it (and any queued note events) to
    // the plugin, and publishes the result. Guarded by pluginMutex so a concurrent
    // getState/setState/setParam/editor command from the main thread below cannot
    // race a process() call
    std::thread audioThread([&] {
        std::vector<float*> outPtrs(static_cast<std::size_t>(args->numChannels));
        int blocksProcessed = 0;

        while (!quitRequested.load()) {
            if (!channel->waitForInput()) {
                quitRequested.store(true);
                break;
            }

            ++blocksProcessed;
            if (args->crashAfter >= 0 && blocksProcessed > args->crashAfter) {
                std::abort();
            }

            int numEvents = 0;
            const MidiEvent* events = channel->events(numEvents);

            juce::MidiBuffer midiBuffer;
            for (int i = 0; i < numEvents; ++i) {
                if (events[i].type == MidiEvent::Type::NoteOn) {
                    midiBuffer.addEvent(juce::MidiMessage::noteOn(1, events[i].number, events[i].value), 0);
                } else if (events[i].type == MidiEvent::Type::NoteOff) {
                    midiBuffer.addEvent(juce::MidiMessage::noteOff(1, events[i].number), 0);
                }
            }

            float* const* inChannels = channel->inputChannels();
            float* const* outChannels = channel->outputChannels();

            {
                std::lock_guard<std::mutex> lock(pluginMutex);

                if (plugin != nullptr) {
                    // Primed with the input first: an effect needs real audio to transform,
                    // an instrument just overwrites it, either way this is correct
                    for (int c = 0; c < args->numChannels; ++c) {
                        outPtrs[static_cast<std::size_t>(c)] = outChannels[c];
                        std::memcpy(outChannels[c], inChannels[c], static_cast<std::size_t>(args->blockSize) * sizeof(float));
                    }
                    AudioBlock block { outPtrs.data(), args->numChannels, args->blockSize };
                    plugin->process(block, &midiBuffer);
                } else if (args->loopback) {
                    for (int c = 0; c < args->numChannels; ++c) {
                        std::memcpy(outChannels[c], inChannels[c], static_cast<std::size_t>(args->blockSize) * sizeof(float));
                    }
                }
            }

            channel->publishOutput();
        }
    });

    // Main thread: pumps the JUCE message loop (editor windows and juce::Timer need it)
    // and polls stdin for control commands between pumps
    while (!quitRequested.load()) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);

        const auto line = tryReadStdinLine();
        if (!line.has_value()) {
            continue;
        }

        const juce::var parsed = juce::JSON::parse(*line);
        const juce::String cmd = parsed.getProperty("cmd", juce::var()).toString();

        if (cmd == "quit") {
            quitRequested.store(true);
        } else if (cmd == "ping") {
            auto* obj = new juce::DynamicObject();
            obj->setProperty("ok", true);
            sendReply(juce::var(obj));
        } else if (cmd == "getParams") {
            std::lock_guard<std::mutex> lock(pluginMutex);
            sendReply(buildParamsReply(plugin != nullptr ? plugin->params() : std::vector<ParamInfo> {}));
        } else if (cmd == "setParam") {
            std::lock_guard<std::mutex> lock(pluginMutex);
            if (plugin != nullptr) {
                const auto index = static_cast<uint32_t>(static_cast<int>(parsed.getProperty("index", 0)));
                const auto value = static_cast<float>(static_cast<double>(parsed.getProperty("value", 0.0)));
                plugin->setParamNormalized(index, value);
            }
        } else if (cmd == "getState") {
            std::lock_guard<std::mutex> lock(pluginMutex);
            juce::MemoryBlock encoded;
            if (plugin != nullptr) {
                const StateBlob state = plugin->saveState();
                encoded.append(state.data(), state.size());
            }
            auto* obj = new juce::DynamicObject();
            obj->setProperty("stateBase64", juce::Base64::toBase64(encoded.getData(), encoded.getSize()));
            sendReply(juce::var(obj));
        } else if (cmd == "setState") {
            std::lock_guard<std::mutex> lock(pluginMutex);
            if (plugin != nullptr) {
                // Base64::toBase64() pairs with Base64::convertFromBase64(), not
                // MemoryBlock::fromBase64Encoding(), see SandboxedPluginInstance::saveState()
                juce::MemoryOutputStream decoded;
                juce::Base64::convertFromBase64(decoded, parsed.getProperty("stateBase64", juce::var()).toString());
                const auto* bytes = static_cast<const uint8_t*>(decoded.getData());
                plugin->loadState(StateBlob(bytes, bytes + decoded.getDataSize()));
            }
        } else if (cmd == "openEditor") {
            std::lock_guard<std::mutex> lock(pluginMutex);
            if (plugin != nullptr && plugin->hasEditor()) {
                if (editorWindow == nullptr) {
                    if (auto* editor = plugin->openEditor()) {
                        editorWindow = std::make_unique<ChildEditorWindow>(args->name, *editor);
                    }
                } else {
                    editorWindow->setVisible(true);
                    editorWindow->toFront(true);
                }
            }
        } else if (cmd == "closeEditor") {
            std::lock_guard<std::mutex> lock(pluginMutex);
            if (editorWindow != nullptr) {
                editorWindow->setVisible(false);
            }
        }
    }

    quitRequested.store(true);
    audioThread.join();

    {
        std::lock_guard<std::mutex> lock(pluginMutex);
        editorWindow.reset();
        if (plugin != nullptr) {
            plugin->closeEditor();
            plugin->release();
        }
    }

    return 0;
}
