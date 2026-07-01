// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: hosts a plugin's native editor in its own top-level window

#include "ui/PluginWindow.h"

#include <functional>

namespace hearth::ui {

namespace {

// Host chrome for a plugin editor, the editor itself is a separate native
// window attached via IPluginInstance::openEditor(), not a child Component
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
        setResizable(true, true);
        centreWithSize(400, 300);
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

// Creates the host window and asks the plugin to embed its editor into it, no-op if the plugin has no editor
void PluginWindow::open() {
    if (m_window != nullptr || !m_plugin.hasEditor()) {
        return;
    }

    m_window = std::make_unique<HostWindow>(m_title, [this] { close(); });
    m_window->setVisible(true);

    m_plugin.openEditor(m_window->getWindowHandle());
}

// Asks the plugin to close its editor, then closes the host window
void PluginWindow::close() {
    if (m_window == nullptr) {
        return;
    }

    m_plugin.closeEditor();
    m_window.reset();
}

} // namespace hearth::ui
