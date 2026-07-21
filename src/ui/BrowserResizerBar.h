// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a thin drag strip on the browser's right edge that sets the column width

#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace howl::ui {

// A thin vertical strip dragged to resize the panel on its left. It reports the drag in
// parent coordinates and resizes nothing itself, so the owner can follow the cursor with a
// light guide line and do the one expensive relayout only when the drag ends
class BrowserResizerBar : public juce::Component {
public:
    // Sets the resize cursor for the whole strip
    BrowserResizerBar() {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

    // Fired when the drag begins, so the owner can show its guide line
    std::function<void()> onDragStart;

    // Fired with the cursor x in parent coordinates on every drag step
    std::function<void(int)> onDrag;

    // Fired with the final cursor x in parent coordinates when the drag ends
    std::function<void(int)> onDragEnd;

    // Paints a faint vertical divider so the drag edge is visible
    void paint(juce::Graphics& graphics) override {
        graphics.setColour(theme::kBorder);
        graphics.fillRect(getWidth() - 1, 0, 1, getHeight());
    }

    // Starts the drag
    void mouseDown(const juce::MouseEvent&) override {
        if (onDragStart != nullptr) {
            onDragStart();
        }
    }

    // Reports the live cursor position so the owner can move its guide line
    void mouseDrag(const juce::MouseEvent& event) override {
        if (onDrag != nullptr && getParentComponent() != nullptr) {
            onDrag(event.getEventRelativeTo(getParentComponent()).x);
        }
    }

    // Commits the final width once the user releases the drag
    void mouseUp(const juce::MouseEvent& event) override {
        if (onDragEnd != nullptr && getParentComponent() != nullptr) {
            onDragEnd(event.getEventRelativeTo(getParentComponent()).x);
        }
    }
};

} // namespace howl::ui
