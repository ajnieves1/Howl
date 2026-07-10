// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the one fixed color palette every view draws from, no user theming

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace howl::ui::theme {

// Deepest layer: arrange/session/rack canvas
inline const juce::Colour kWindowBg { 0xFF17191C };

// Panels: transport bar, track headers, browser, mixer, bottom panels
inline const juce::Colour kPanelBg { 0xFF1F2226 };

// Buttons, combos, step cells at rest
inline const juce::Colour kRaisedBg { 0xFF282C31 };

// Hover state on any interactive surface
inline const juce::Colour kHoverBg { 0xFF333940 };

// Hairlines between panels, cell and clip outlines
inline const juce::Colour kBorder { 0xFF3A4046 };

inline const juce::Colour kTextPrimary { 0xFFE6E9EC };
inline const juce::Colour kTextSecondary { 0xFF9AA1A8 };
inline const juce::Colour kTextDisabled { 0xFF5C636B };

// Warm amber: MIDI clips, primary buttons, active toggles, filled steps
inline const juce::Colour kAccent { 0xFFE98A3C };

// Teal: audio clips and waveforms
inline const juce::Colour kAudio { 0xFF4E9DA8 };

// Violet: pattern blocks and the pattern lane tint
inline const juce::Colour kPattern { 0xFF9B7BD4 };

// Selection borders and the marquee
inline const juce::Colour kSelection { 0xFFF5D06B };

inline const juce::Colour kPlayhead { 0xFFF2F3F4 };

// Crash/destructive indicators; also the meter's peak color
inline const juce::Colour kDanger { 0xFFD95757 };

// Meter level below peak
inline const juce::Colour kMeter { 0xFF6FBF73 };

// Section headers, base UI text, and small labels, on the 4 px spacing grid
constexpr float kFontSizeHeader = 15.0f;
constexpr float kFontSizeBase = 13.0f;
constexpr float kFontSizeSmall = 11.0f;
constexpr int kSpacingUnit = 4;
constexpr int kPanelPadding = 8;

} // namespace howl::ui::theme
