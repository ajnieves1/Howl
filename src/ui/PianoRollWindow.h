// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a reusable top level window that hosts the piano roll outside the main shell

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace howl::ui {

// Hosts one piano roll at a time in its own resizable window. The window clears the taskbar
// flag so the desktop groups it under the main window instead of listing a second application
class PianoRollWindow : public juce::DocumentWindow {
public:
    // Builds the window hidden, storing the callback run when the user closes it
    explicit PianoRollWindow(std::function<void()> onCloseRequested);

    // Takes ownership of roll as the window content, sets the title, then shows and raises it
    void showRoll(std::unique_ptr<juce::Component> roll, const juce::String& title);

    // Returns the base style flags with the taskbar button flag cleared
    int getDesktopWindowStyleFlags() const override;

    // Runs the stored close callback
    void closeButtonPressed() override;

private:
    std::function<void()> m_onCloseRequested;
    bool m_sized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollWindow)
};

} // namespace howl::ui
