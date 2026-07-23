// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: applies the fixed theme to every stock JUCE widget the app uses

#include "ui/HowlLookAndFeel.h"

#include "ui/Theme.h"

namespace howl::ui {

// Sets the LookAndFeel_V4 color scheme and per widget color ids from Theme.h
HowlLookAndFeel::HowlLookAndFeel() {
    setColourScheme({
        theme::kWindowBg,    // windowBackground
        theme::kRaisedBg,    // widgetBackground
        theme::kPanelBg,     // menuBackground
        theme::kBorder,      // outline
        theme::kTextPrimary, // defaultText
        theme::kRaisedBg,    // defaultFill
        theme::kWindowBg,    // highlightedText
        theme::kAccent,      // highlightedFill
        theme::kTextPrimary  // menuText
    });

    setColour(juce::ResizableWindow::backgroundColourId, theme::kWindowBg);
    setColour(juce::DocumentWindow::textColourId, theme::kTextPrimary);

    setColour(juce::TextButton::buttonColourId, theme::kRaisedBg);
    setColour(juce::TextButton::buttonOnColourId, theme::kAccent);
    setColour(juce::TextButton::textColourOffId, theme::kTextPrimary);
    setColour(juce::TextButton::textColourOnId, theme::kWindowBg);

    setColour(juce::ComboBox::backgroundColourId, theme::kRaisedBg);
    setColour(juce::ComboBox::textColourId, theme::kTextPrimary);
    setColour(juce::ComboBox::outlineColourId, theme::kBorder);
    setColour(juce::ComboBox::buttonColourId, theme::kRaisedBg);
    setColour(juce::ComboBox::arrowColourId, theme::kTextSecondary);
    setColour(juce::ComboBox::focusedOutlineColourId, theme::kAccent);

    setColour(juce::PopupMenu::backgroundColourId, theme::kPanelBg);
    setColour(juce::PopupMenu::textColourId, theme::kTextPrimary);
    setColour(juce::PopupMenu::headerTextColourId, theme::kTextSecondary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::kAccent);
    setColour(juce::PopupMenu::highlightedTextColourId, theme::kWindowBg);

    setColour(juce::ScrollBar::backgroundColourId, theme::kPanelBg);
    setColour(juce::ScrollBar::thumbColourId, theme::kBorder);
    setColour(juce::ScrollBar::trackColourId, theme::kPanelBg);

    setColour(juce::TextEditor::backgroundColourId, theme::kRaisedBg);
    setColour(juce::TextEditor::textColourId, theme::kTextPrimary);
    setColour(juce::TextEditor::highlightColourId, theme::kAccent);
    setColour(juce::TextEditor::highlightedTextColourId, theme::kWindowBg);
    setColour(juce::TextEditor::outlineColourId, theme::kBorder);
    setColour(juce::TextEditor::focusedOutlineColourId, theme::kAccent);

    setColour(juce::AlertWindow::backgroundColourId, theme::kPanelBg);
    setColour(juce::AlertWindow::textColourId, theme::kTextPrimary);
    setColour(juce::AlertWindow::outlineColourId, theme::kBorder);

    setColour(juce::Label::textColourId, theme::kTextPrimary);
    setColour(juce::Label::outlineColourId, theme::kBorder);
}

} // namespace howl::ui
