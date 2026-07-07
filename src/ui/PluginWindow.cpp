// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: hosts a plugin's native editor in its own top-level window

#include "ui/PluginWindow.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace howl::ui {

namespace {

// Host chrome for a plugin editor, the editor is hosted as the window's
// content component so the window always matches the editor's size
class HostWindow : public juce::DocumentWindow {
public:
    // Stores the callback to run when the user clicks the close button
    HostWindow(const juce::String& title, std::function<void()> onCloseRequested)
        : DocumentWindow(title,
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour(juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostWindow)
};

} // namespace

// Stores the plugin to host, does not open anything yet
PluginWindow::PluginWindow(plugins::IPluginInstance& plugin, const juce::String& title)
    : m_plugin(plugin)
    , m_title(title)
{
}

// Closes the window if still open
PluginWindow::~PluginWindow() {
    close();
}

// Creates the host window with the plugin's editor as its content, no-op if the plugin has no editor
void PluginWindow::open() {
    if (m_window != nullptr || !m_plugin.hasEditor()) {
        return;
    }

    juce::Component* editor = m_plugin.openEditor();
    if (editor == nullptr) {
        return;
    }

    m_window = std::make_unique<HostWindow>(m_title, [this] { close(); });

    // The window sizes itself around the editor now and follows any resize
    // the editor makes later, the adapter keeps ownership of the editor
    m_window->setContentNonOwned(editor, true);

    if (auto* processorEditor = dynamic_cast<juce::AudioProcessorEditor*>(editor)) {
        m_window->setResizable(processorEditor->isResizable(), false);
    }

    m_window->centreWithSize(m_window->getWidth(), m_window->getHeight());
    m_window->setVisible(true);
    m_window->toFront(true);
}

// Detaches the editor from the window, then lets the plugin destroy it
void PluginWindow::close() {
    if (m_window == nullptr) {
        return;
    }

    m_window->clearContentComponent();
    m_window.reset();
    m_plugin.closeEditor();
}

} // namespace howl::ui
