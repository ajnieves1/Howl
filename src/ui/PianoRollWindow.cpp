// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a reusable top level window that hosts the piano roll outside the main shell

#include "ui/PianoRollWindow.h"

namespace howl::ui {

// Builds the window hidden, storing the callback run when the user closes it. It is not added
// to the desktop here so the taskbar flag can be cleared through the override below on the first
// show, when the derived override is live rather than the base class version
PianoRollWindow::PianoRollWindow(std::function<void()> onCloseRequested)
    : DocumentWindow("Piano Roll",
                     juce::Desktop::getInstance().getDefaultLookAndFeel()
                         .findColour(juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons,
                     false)
    , m_onCloseRequested(std::move(onCloseRequested))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);

    // Without this the window manager drops the roll behind the main window as soon as focus
    // moves back to it, and nothing short of reopening a clip brings it forward again
    setAlwaysOnTop(true);
}

// Takes ownership of roll as the window content, sets the title, then shows and raises it
void PianoRollWindow::showRoll(std::unique_ptr<juce::Component> roll, const juce::String& title) {
    if (!isOnDesktop()) {
        addToDesktop(getDesktopWindowStyleFlags());
    }

    setName(title);

    juce::Component* content = roll.release();
    content->setWantsKeyboardFocus(true);
    setContentOwned(content, false);

    if (!m_sized) {
        centreWithSize(1000, 560);
        m_sized = true;
    }

    setVisible(true);
    toFront(true);
    content->grabKeyboardFocus();
}

// Returns the base style flags with the taskbar button flag cleared, so the desktop keeps this
// window grouped with the main application instead of listing it as a separate entry
int PianoRollWindow::getDesktopWindowStyleFlags() const {
    return DocumentWindow::getDesktopWindowStyleFlags() & ~juce::ComponentPeer::windowAppearsOnTaskbar;
}

// Runs the stored close callback
void PianoRollWindow::closeButtonPressed() {
    if (m_onCloseRequested) {
        m_onCloseRequested();
    }
}

} // namespace howl::ui
