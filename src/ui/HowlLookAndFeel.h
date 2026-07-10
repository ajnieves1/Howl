// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: applies the fixed theme to every stock JUCE widget the app uses

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace howl::ui {

// Applies the theme to every stock JUCE widget the app uses
class HowlLookAndFeel : public juce::LookAndFeel_V4 {
public:
    // Sets the LookAndFeel_V4 colour scheme and per-widget colour ids from Theme.h
    HowlLookAndFeel();
};

} // namespace howl::ui
