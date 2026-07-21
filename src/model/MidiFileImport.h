// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: reads a Standard MIDI File into one MidiClip at the project tick resolution

#pragma once

#include "model/MidiClip.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace howl::model {

// Reads a Standard MIDI File and flattens every track into one MidiClip at
// kTicksPerQuarter resolution, returns nullopt when the file will not parse or
// carries no notes
std::optional<MidiClip> importMidiFileAsClip(const juce::File& file);

} // namespace howl::model
