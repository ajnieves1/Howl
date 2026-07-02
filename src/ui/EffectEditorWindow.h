// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a floating generic editor, one labeled 0..1 slider per effect parameter

#pragma once

#include "engine/Effect.h"
#include "plugins/IPluginInstance.h"
#include "ui/PluginWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace howl::ui {

// Floating generic editor: one labeled 0..1 slider per effect parameter
class EffectEditorWindow : public juce::DocumentWindow {
public:
    // nativeInstance may be null; when non-null and hasEditor(), an Open Plugin Editor button appears
    EffectEditorWindow(engine::Effect& effect, plugins::IPluginInstance* nativeInstance);

    // The content component (and any open native plugin editor it owns) is torn down automatically
    ~EffectEditorWindow() override;

    // Hides the window, the owning strip destroys it on the next structural edit
    void closeButtonPressed() override;

private:
    // One parameter row: name label plus a 0..1 slider
    struct ParamRow {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
    };

    // The window's content: an optional native-editor button, then one row per parameter
    class Content : public juce::Component {
    public:
        Content(engine::Effect& effect, plugins::IPluginInstance* nativeInstance);
        void resized() override;

    private:
        static constexpr int kRowHeight = 24;

        engine::Effect& m_effect;
        plugins::IPluginInstance* m_nativeInstance;
        std::unique_ptr<juce::TextButton> m_openPluginEditorButton;
        std::unique_ptr<PluginWindow> m_pluginWindow;
        std::vector<ParamRow> m_rows;
    };
};

} // namespace howl::ui
