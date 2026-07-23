// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: loads and wraps a CLAP instance behind IPluginInstance

#include "plugins/ClapAdapter.h"

#include "core/LockFreeQueue.h"

#include <clap/clap.h>
#include <clap/helpers/event-list.hh>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace howl::plugins {

namespace {

// The window api this platform embeds through, CLAP names one per windowing system
#if JUCE_WINDOWS
constexpr const char* kWindowApi = CLAP_WINDOW_API_WIN32;
#elif JUCE_MAC
constexpr const char* kWindowApi = CLAP_WINDOW_API_COCOA;
#else
constexpr const char* kWindowApi = CLAP_WINDOW_API_X11;
#endif

// Fills a clap_window for this platform from a native peer handle
clap_window_t makeClapWindow(void* nativeHandle) {
    clap_window_t window {};
    window.api = kWindowApi;
#if JUCE_WINDOWS
    window.win32 = nativeHandle;
#elif JUCE_MAC
    window.cocoa = nativeHandle;
#else
    window.x11 = static_cast<clap_xwnd>(reinterpret_cast<uintptr_t>(nativeHandle));
#endif
    return window;
}

// Hosts an embedded CLAP gui: the plugin parents its own window into this component as soon as
// the component has a native peer to hand it, which is only true once it is on screen
class ClapEditorComponent : public juce::Component, private juce::Timer {
public:
    // Stores the plugin and its gui extension and takes the plugin's preferred size
    ClapEditorComponent(const clap_plugin_t* plugin, const clap_plugin_gui_t* gui, int width, int height)
        : m_plugin(plugin)
        , m_gui(gui)
    {
        setSize(width, height);

        // A child component has no native handle until its window reaches the screen, and no
        // callback fires on the child at that moment, so poll until the handle exists
        startTimerHz(30);
    }

    // Stops the attach poll
    ~ClapEditorComponent() override {
        stopTimer();
    }

    // Tries to attach once this component joins a window
    void parentHierarchyChanged() override {
        attachToPlugin();
    }

    // Tries to attach once this component becomes visible
    void visibilityChanged() override {
        attachToPlugin();
    }

    // Keeps the plugin's window matching this component when the host resizes it
    void resized() override {
        if (m_attached && m_gui != nullptr && m_gui->set_size != nullptr) {
            m_gui->set_size(m_plugin, static_cast<uint32_t>(getWidth()), static_cast<uint32_t>(getHeight()));
        }
    }

private:
    // Retries the attach until the component actually has a native window to hand over
    void timerCallback() override {
        attachToPlugin();
    }

    // Hands the plugin this component's native window to parent into, then shows the gui
    void attachToPlugin() {
        if (m_attached || m_plugin == nullptr || m_gui == nullptr || m_gui->set_parent == nullptr) {
            return;
        }

        void* handle = getWindowHandle();
        if (handle == nullptr) {
            return; // not on screen yet, the timer tries again
        }

        const clap_window_t window = makeClapWindow(handle);
        if (!m_gui->set_parent(m_plugin, &window)) {
            juce::Logger::writeToLog("Howl: CLAP set_parent failed, the plugin refused this window");
            stopTimer();
            return;
        }

        m_attached = true;
        stopTimer();

        if (m_gui->show != nullptr && !m_gui->show(m_plugin)) {
            juce::Logger::writeToLog("Howl: CLAP show failed after parenting the editor");
        }
    }

    const clap_plugin_t* m_plugin = nullptr;
    const clap_plugin_gui_t* m_gui = nullptr;
    bool m_attached = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClapEditorComponent)
};

// Returns the plugin's gui extension, null when it exposes none
const clap_plugin_gui_t* guiFor(const clap_plugin_t* plugin) {
    if (plugin == nullptr) {
        return nullptr;
    }
    return static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin, CLAP_EXT_GUI));
}

// The plugin's resize hints changed, this host reads them fresh when it needs them
void CLAP_ABI hostGuiResizeHintsChanged(const clap_host_t*) {
}

// The plugin asks to resize its window, refused for now so it keeps its own reported size
bool CLAP_ABI hostGuiRequestResize(const clap_host_t*, uint32_t, uint32_t) {
    return false;
}

// The plugin asks to be shown, this host shows the gui when the editor is opened instead
bool CLAP_ABI hostGuiRequestShow(const clap_host_t*) {
    return false;
}

// The plugin asks to be hidden, this host hides the gui when the editor is closed instead
bool CLAP_ABI hostGuiRequestHide(const clap_host_t*) {
    return false;
}

// The plugin closed its own gui, nothing for this host to unwind
void CLAP_ABI hostGuiClosed(const clap_host_t*, bool) {
}

// The plugin reports a failed preset load, logged so a bad drop is not silent
void CLAP_ABI hostPresetLoadOnError(const clap_host_t*, uint32_t, const char* location, const char*,
                                    int32_t, const char* msg) {
    juce::Logger::writeToLog(juce::String("Howl: CLAP preset load failed for ")
        + (location != nullptr ? location : "") + ": " + (msg != nullptr ? msg : ""));
}

// The plugin reports a successful preset load, nothing for this host to sync
void CLAP_ABI hostPresetLoadLoaded(const clap_host_t*, uint32_t, const char*, const char*) {
}

// Host callbacks, a minimal offline host that offers only the preset-load extension so plugins
// that require it will load a preset from a file path
const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* extensionId) {
    if (std::strcmp(extensionId, CLAP_EXT_PRESET_LOAD) == 0
        || std::strcmp(extensionId, CLAP_EXT_PRESET_LOAD_COMPAT) == 0) {
        static const clap_host_preset_load_t presetLoad { hostPresetLoadOnError, hostPresetLoadLoaded };
        return &presetLoad;
    }
    if (std::strcmp(extensionId, CLAP_EXT_GUI) == 0) {
        static const clap_host_gui_t gui { hostGuiResizeHintsChanged, hostGuiRequestResize,
            hostGuiRequestShow, hostGuiRequestHide, hostGuiClosed };
        return &gui;
    }
    return nullptr;
}

// The plugin has asked to be reactivated, ignored by this minimal host
void CLAP_ABI hostRequestRestart(const clap_host_t*) {
}

// The plugin has asked for a process call, ignored by this minimal host
void CLAP_ABI hostRequestProcess(const clap_host_t*) {
}

// The plugin has asked for a main-thread callback, ignored by this minimal host
void CLAP_ABI hostRequestCallback(const clap_host_t*) {
}

// Appends written bytes to the StateBlob pointed to by the stream context
int64_t CLAP_ABI ostreamWrite(const clap_ostream_t* stream, const void* buffer, uint64_t size) {
    auto* blob = static_cast<StateBlob*>(stream->ctx);
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    blob->insert(blob->end(), bytes, bytes + size);
    return static_cast<int64_t>(size);
}

// Tracks the read cursor into a StateBlob during loadState
struct ReadContext {
    const StateBlob* blob;
    std::size_t pos;
};

// Copies the next bytes out of the StateBlob pointed to by the stream context
int64_t CLAP_ABI istreamRead(const clap_istream_t* stream, void* buffer, uint64_t size) {
    auto* ctx = static_cast<ReadContext*>(stream->ctx);
    const std::size_t remaining = ctx->blob->size() - ctx->pos;
    const std::size_t count = std::min(static_cast<std::size_t>(size), remaining);
    if (count > 0) {
        std::memcpy(buffer, ctx->blob->data() + ctx->pos, count);
        ctx->pos += count;
    }
    return static_cast<int64_t>(count);
}

// True if the descriptor lists the "instrument" feature
bool descriptorIsInstrument(const clap_plugin_descriptor_t* desc) {
    if (desc->features == nullptr) {
        return false;
    }
    for (const char* const* feature = desc->features; *feature != nullptr; ++feature) {
        if (std::strcmp(*feature, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0) {
            return true;
        }
    }
    return false;
}

// The directories CLAP plugins are conventionally installed into
std::vector<std::string> standardClapDirectories() {
    std::vector<std::string> dirs;
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

   #if JUCE_LINUX
    dirs.push_back(home.getChildFile(".clap").getFullPathName().toStdString());
    dirs.push_back("/usr/lib/clap");
    dirs.push_back("/usr/local/lib/clap");
    dirs.push_back("/usr/lib64/clap");
   #elif JUCE_MAC
    dirs.push_back(home.getChildFile("Library/Audio/Plug-Ins/CLAP").getFullPathName().toStdString());
    dirs.push_back("/Library/Audio/Plug-Ins/CLAP");
   #elif JUCE_WINDOWS
    if (const char* common = std::getenv("COMMONPROGRAMFILES")) {
        dirs.push_back(std::string(common) + "\\CLAP");
    }
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        dirs.push_back(std::string(local) + "\\Programs\\Common\\CLAP");
    }
   #endif

    return dirs;
}

// Opens one .clap file, appends every plugin it declares to out
void scanOneClapFile(const std::string& path, std::vector<ClapPluginInfo>& out) {
    juce::DynamicLibrary library;
    if (!library.open(path)) {
        return;
    }

    auto* entry = static_cast<const clap_plugin_entry_t*>(library.getFunction("clap_entry"));
    if (entry == nullptr || !entry->init(path.c_str())) {
        library.close();
        return;
    }

    if (auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID))) {
        const uint32_t count = factory->get_plugin_count(factory);
        for (uint32_t i = 0; i < count; ++i) {
            const auto* desc = factory->get_plugin_descriptor(factory, i);
            if (desc == nullptr) {
                continue;
            }
            out.push_back(ClapPluginInfo {
                path,
                desc->id != nullptr ? desc->id : "",
                desc->name != nullptr ? desc->name : "",
                desc->vendor != nullptr ? desc->vendor : "",
                descriptorIsInstrument(desc)
            });
        }
    }

    entry->deinit();
    library.close();
}

} // namespace

// Holds everything that touches the CLAP C ABI, kept out of the header
struct ClapAdapter::Impl {
    // One queued parameter change, plain value already mapped from normalized
    struct PendingParam {
        clap_id id;
        double value;
    };

    // Tears the plugin down in the correct order, then unloads the library
    ~Impl() {
        if (plugin != nullptr) {
            if (processing) {
                plugin->stop_processing(plugin);
            }
            if (activated) {
                plugin->deactivate(plugin);
            }
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        if (entry != nullptr) {
            entry->deinit();
            entry = nullptr;
        }
    }

    juce::DynamicLibrary library;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* plugin = nullptr;
    clap_host_t host {};

    bool activated = false;
    bool processing = false;
    int64_t steadyTime = 0;

    clap::helpers::EventList inEvents;
    clap::helpers::EventList outEvents;

    std::vector<clap_id> paramIds;
    std::vector<double> paramMin;
    std::vector<double> paramMax;

    LockFreeQueue<PendingParam, 1024> pending;
};

// Takes ownership of a fully loaded implementation
ClapAdapter::ClapAdapter(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

// Closes the plugin and unloads its library
ClapAdapter::~ClapAdapter() {
    // The gui must be destroyed while the plugin is still alive, Impl tears the plugin down after
    closeEditor();
}

// Searches the standard CLAP directories and returns every plugin found
std::vector<ClapPluginInfo> ClapAdapter::scan() {
    std::vector<ClapPluginInfo> found;
    for (const auto& dir : standardClapDirectories()) {
        juce::File directory(dir);
        if (!directory.isDirectory()) {
            continue;
        }
        const auto files = directory.findChildFiles(juce::File::findFilesAndDirectories, true, "*.clap");
        for (const auto& file : files) {
            scanOneClapFile(file.getFullPathName().toStdString(), found);
        }
    }
    return found;
}

// Opens one .clap file directly and returns every plugin it declares, used by the
// sandbox child to resolve a plugin id from just the path and name it was given
std::vector<ClapPluginInfo> ClapAdapter::scanFile(const std::string& path) {
    std::vector<ClapPluginInfo> found;
    scanOneClapFile(path, found);
    return found;
}

// Loads the given CLAP plugin, returns nullptr on any failure
std::unique_ptr<ClapAdapter> ClapAdapter::load(const ClapPluginInfo& info) {
    auto impl = std::make_unique<Impl>();

    if (!impl->library.open(info.path)) {
        return nullptr;
    }

    auto* entry = static_cast<const clap_plugin_entry_t*>(impl->library.getFunction("clap_entry"));
    if (entry == nullptr || !entry->init(info.path.c_str())) {
        return nullptr;
    }
    impl->entry = entry;

    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr) {
        return nullptr;
    }

    impl->host.clap_version = CLAP_VERSION;
    impl->host.host_data = impl.get();
    impl->host.name = "Howl";
    impl->host.vendor = "Howl";
    impl->host.url = "https://github.com/ajnieves1/FOSSDaw";
    impl->host.version = "0.1.0";
    impl->host.get_extension = hostGetExtension;
    impl->host.request_restart = hostRequestRestart;
    impl->host.request_process = hostRequestProcess;
    impl->host.request_callback = hostRequestCallback;

    const clap_plugin_t* plugin = factory->create_plugin(factory, &impl->host, info.id.c_str());
    if (plugin == nullptr) {
        return nullptr;
    }

    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return nullptr;
    }
    impl->plugin = plugin;

    return std::unique_ptr<ClapAdapter>(new ClapAdapter(std::move(impl)));
}

// Activates and starts processing at the given rate and block size
void ClapAdapter::prepare(double sampleRate, int maxBlockSize) {
    auto* plugin = m_impl->plugin;

    if (auto* ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS))) {
        if (ports->count(plugin, false) > 0) {
            clap_audio_port_info_t info {};
            if (ports->get(plugin, 0, false, &info)) {
                // Channel layout is read for completeness, the caller still
                // decides the block's channel count at process() time
                juce::ignoreUnused(info);
            }
        }
    }

    // min_frames_count must be at least 1 per the CLAP spec, 1 accepts any block size up to max
    if (plugin->activate(plugin, sampleRate, 1, static_cast<uint32_t>(maxBlockSize))) {
        m_impl->activated = true;
        if (plugin->start_processing(plugin)) {
            m_impl->processing = true;
        }
    }

    m_impl->inEvents.reserveHeap(64 * 1024);
    m_impl->inEvents.reserveEvents(4096);
    m_impl->steadyTime = 0;

    m_params.clear();
    m_impl->paramIds.clear();
    m_impl->paramMin.clear();
    m_impl->paramMax.clear();

    if (auto* paramsExt = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS))) {
        const uint32_t count = paramsExt->count(plugin);
        for (uint32_t i = 0; i < count; ++i) {
            clap_param_info_t info {};
            if (!paramsExt->get_info(plugin, i, &info)) {
                continue;
            }
            const double range = info.max_value - info.min_value;
            const float defaultNormalized = range > 0.0
                ? static_cast<float>((info.default_value - info.min_value) / range)
                : 0.0f;
            m_params.push_back(ParamInfo {
                static_cast<uint32_t>(m_impl->paramIds.size()),
                info.name,
                defaultNormalized
            });
            m_impl->paramIds.push_back(info.id);
            m_impl->paramMin.push_back(info.min_value);
            m_impl->paramMax.push_back(info.max_value);
        }
    }
}

// Stops processing and deactivates
void ClapAdapter::release() {
    auto* plugin = m_impl->plugin;
    if (m_impl->processing) {
        plugin->stop_processing(plugin);
        m_impl->processing = false;
    }
    if (m_impl->activated) {
        plugin->deactivate(plugin);
        m_impl->activated = false;
    }
}

// [RT] midiIn must point to a const juce::MidiBuffer, may be nullptr
void ClapAdapter::process(AudioBlock& audio, const void* midiIn) {
    auto* plugin = m_impl->plugin;
    auto& inEvents = m_impl->inEvents;
    auto& outEvents = m_impl->outEvents;

    inEvents.clear();
    outEvents.clear();

    // Parameter changes queued from other threads, all applied at time 0
    Impl::PendingParam pending {};
    while (m_impl->pending.pop(pending)) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = 0;
        event.param_id = pending.id;
        event.cookie = nullptr;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = pending.value;
        inEvents.push(&event.header);
    }

    // Translate MIDI note on/off into CLAP note events, other MIDI is ignored for now
    if (midiIn != nullptr) {
        const auto* midi = static_cast<const juce::MidiBuffer*>(midiIn);
        for (const auto metadata : *midi) {
            const auto message = metadata.getMessage();
            if (!message.isNoteOnOrOff()) {
                continue;
            }
            clap_event_note_t event {};
            event.header.size = sizeof(event);
            event.header.time = static_cast<uint32_t>(metadata.samplePosition);
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = message.isNoteOn()
                ? static_cast<uint16_t>(CLAP_EVENT_NOTE_ON)
                : static_cast<uint16_t>(CLAP_EVENT_NOTE_OFF);
            event.header.flags = 0;
            event.note_id = -1;
            event.port_index = 0;
            event.channel = static_cast<int16_t>(message.getChannel() - 1);
            event.key = static_cast<int16_t>(message.getNoteNumber());
            event.velocity = message.getFloatVelocity();
            inEvents.push(&event.header);
        }
    }

    clap_audio_buffer_t out {};
    out.data32 = audio.channels;
    out.data64 = nullptr;
    out.channel_count = static_cast<uint32_t>(audio.numChannels);
    out.latency = 0;
    out.constant_mask = 0;

    clap_process_t proc {};
    proc.steady_time = m_impl->steadyTime;
    proc.frames_count = static_cast<uint32_t>(audio.numFrames);
    proc.transport = nullptr;
    proc.audio_inputs = nullptr;
    proc.audio_inputs_count = 0;
    proc.audio_outputs = &out;
    proc.audio_outputs_count = 1;
    proc.in_events = inEvents.clapInputEvents();
    proc.out_events = outEvents.clapOutputEvents();

    plugin->process(plugin, &proc);
    m_impl->steadyTime += audio.numFrames;
}

// Serializes the plugin's state via the CLAP state extension
StateBlob ClapAdapter::saveState() const {
    StateBlob blob;
    auto* plugin = m_impl->plugin;
    if (auto* stateExt = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE))) {
        clap_ostream_t stream {};
        stream.ctx = &blob;
        stream.write = ostreamWrite;
        stateExt->save(plugin, &stream);
    }
    return blob;
}

// Restores a previously serialized state
void ClapAdapter::loadState(const StateBlob& state) {
    auto* plugin = m_impl->plugin;
    if (auto* stateExt = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE))) {
        ReadContext ctx { &state, 0 };
        clap_istream_t stream {};
        stream.ctx = &ctx;
        stream.read = istreamRead;
        stateExt->load(plugin, &stream);
    }
}

// Asks the plugin to load a preset file through the CLAP preset-load extension, false when the
// plugin does not expose it or the load fails
bool ClapAdapter::loadPresetFile(const juce::File& file) {
    const clap_plugin_t* plugin = m_impl->plugin;
    if (plugin == nullptr) {
        return false;
    }

    const auto* presetLoad = static_cast<const clap_plugin_preset_load_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD));
    if (presetLoad == nullptr) {
        presetLoad = static_cast<const clap_plugin_preset_load_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD_COMPAT));
    }
    if (presetLoad == nullptr || presetLoad->from_location == nullptr) {
        return false;
    }

    return presetLoad->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
        file.getFullPathName().toRawUTF8(), nullptr);
}

// Returns the snapshot taken by the last prepare() call
const std::vector<ParamInfo>& ClapAdapter::params() const {
    return m_params;
}

// Queues a normalized parameter change for the next process() call
void ClapAdapter::setParamNormalized(uint32_t id, float value) {
    if (id >= m_impl->paramIds.size()) {
        return;
    }
    const double min = m_impl->paramMin[id];
    const double max = m_impl->paramMax[id];
    const double plain = min + static_cast<double>(value) * (max - min);
    m_impl->pending.push(Impl::PendingParam { m_impl->paramIds[id], plain });
}

// CLAP editor hosting is a later task, always false for now
bool ClapAdapter::hasEditor() const {
    const clap_plugin_gui_t* gui = guiFor(m_impl->plugin);
    if (gui == nullptr || gui->create == nullptr || gui->is_api_supported == nullptr) {
        return false;
    }

    return gui->is_api_supported(m_impl->plugin, kWindowApi, false)
        || gui->is_api_supported(m_impl->plugin, kWindowApi, true);
}

// Creates the plugin's gui, embedding it into a component when the plugin supports that, else
// letting the plugin open its own floating window and returning nothing to host
juce::Component* ClapAdapter::openEditor() {
    if (m_editor != nullptr) {
        return m_editor.get();
    }

    const clap_plugin_gui_t* gui = guiFor(m_impl->plugin);
    if (gui == nullptr || gui->create == nullptr || gui->is_api_supported == nullptr) {
        return nullptr;
    }

    if (gui->is_api_supported(m_impl->plugin, kWindowApi, false)) {
        if (!gui->create(m_impl->plugin, kWindowApi, false)) {
            return nullptr;
        }
        m_guiCreated = true;

        uint32_t width = 800;
        uint32_t height = 600;
        if (gui->get_size != nullptr) {
            gui->get_size(m_impl->plugin, &width, &height);
        }

        m_editor = std::make_unique<ClapEditorComponent>(m_impl->plugin, gui,
            static_cast<int>(width), static_cast<int>(height));
        return m_editor.get();
    }

    // Floating: the plugin owns the window, there is nothing for the host to embed
    if (gui->is_api_supported(m_impl->plugin, kWindowApi, true)) {
        if (!gui->create(m_impl->plugin, kWindowApi, true)) {
            return nullptr;
        }
        m_guiCreated = true;

        if (gui->suggest_title != nullptr) {
            gui->suggest_title(m_impl->plugin, "Howl");
        }
        if (gui->show != nullptr) {
            gui->show(m_impl->plugin);
        }
    }

    return nullptr;
}

// Hides and destroys the plugin's gui and drops the host side component
void ClapAdapter::closeEditor() {
    const clap_plugin_gui_t* gui = guiFor(m_impl->plugin);

    m_editor.reset();

    if (gui != nullptr && m_guiCreated) {
        if (gui->hide != nullptr) {
            gui->hide(m_impl->plugin);
        }
        if (gui->destroy != nullptr) {
            gui->destroy(m_impl->plugin);
        }
    }

    m_guiCreated = false;
}

} // namespace howl::plugins
