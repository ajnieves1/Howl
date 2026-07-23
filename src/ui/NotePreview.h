// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: draws a clip's notes as a mini roll, shared by the timeline and the channel rack

#pragma once

#include "model/MidiClip.h"
#include "model/Note.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>

namespace howl::ui {

// Draws a clip's notes as a mini roll inside bounds, scaled to the notes' own key span so a
// narrow melody still fills the height. Does nothing when the area is too small to read
inline void paintNotePreview(juce::Graphics& g, const model::MidiClip& clip, int64_t lengthTicks,
                             juce::Rectangle<float> bounds, juce::Colour color) {
    const auto& notes = clip.notes();
    if (notes.empty() || lengthTicks <= 0 || bounds.getWidth() < 6.0f || bounds.getHeight() < 5.0f) {
        return;
    }

    int minKey = 127;
    int maxKey = 0;
    for (const model::Note& note : notes) {
        minKey = std::min(minKey, note.key);
        maxKey = std::max(maxKey, note.key);
    }

    // A single pitch would otherwise divide by zero, one semitone of span parks it mid height
    const float span = static_cast<float>(std::max(1, maxKey - minKey));
    const float noteHeight = juce::jlimit(1.0f, 3.0f, bounds.getHeight() / (span + 1.0f));

    g.setColour(color);
    for (const model::Note& note : notes) {
        const float x = bounds.getX()
            + static_cast<float>(note.startTick) / static_cast<float>(lengthTicks) * bounds.getWidth();
        if (x >= bounds.getRight()) {
            continue;
        }

        const float width = static_cast<float>(note.lengthTicks) / static_cast<float>(lengthTicks)
            * bounds.getWidth();
        const float ratio = static_cast<float>(note.key - minKey) / span;
        const float y = bounds.getBottom() - noteHeight - ratio * (bounds.getHeight() - noteHeight);

        g.fillRect(juce::Rectangle<float> { x, y,
            juce::jlimit(1.0f, bounds.getRight() - x, width), noteHeight });
    }
}

} // namespace howl::ui
