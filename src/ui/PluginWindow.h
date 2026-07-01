// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: hosts a plugin's native editor in its own top-level window

#pragma once

#include "plugins/IPluginInstance.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace howl::ui {

class PluginWindow {
public:
    // Stores the plugin to host, does not open anything yet
    PluginWindow(plugins::IPluginInstance& plugin, const juce::String& title);

    // Closes the window if still open
    ~PluginWindow();

    // Creates the host window and asks the plugin to embed its editor into it, no-op if the plugin has no editor
    void open();

    // Asks the plugin to close its editor, then closes the host window
    void close();

private:
    plugins::IPluginInstance& m_plugin;
    juce::String m_title;
    std::unique_ptr<juce::DocumentWindow> m_window;
};

} // namespace howl::ui
