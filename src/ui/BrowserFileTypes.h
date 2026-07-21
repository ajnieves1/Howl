// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shared file type tests so the browser and its drop targets agree

#pragma once

#include <juce_core/juce_core.h>

namespace howl::ui::filetypes {

// True when the extension is a sample the preview player and audio clips can read
inline bool isAudioFile(const juce::File& file) {
    return file.hasFileExtension("wav");
}

// True when the extension is a Standard MIDI File
inline bool isMidiFile(const juce::File& file) {
    return file.hasFileExtension("mid;midi");
}

// True when the extension is a synth patch or preset container
inline bool isPatchFile(const juce::File& file) {
    return file.hasFileExtension("fxp;fxb;vstpreset;vital;serumpreset;serumpack");
}

} // namespace howl::ui::filetypes
