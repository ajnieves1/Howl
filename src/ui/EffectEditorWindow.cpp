// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: a floating generic editor, one labeled 0..1 slider per effect parameter

#include "ui/EffectEditorWindow.h"

namespace howl::ui {

// Builds the content component and sizes the window to fit its rows
EffectEditorWindow::EffectEditorWindow(engine::Effect& effect, plugins::IPluginInstance* nativeInstance)
    : DocumentWindow(
          effect.displayName(),
          juce::Desktop::getInstance().getDefaultLookAndFeel()
              .findColour(juce::ResizableWindow::backgroundColourId),
          DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);

    const bool hasPluginButton = nativeInstance != nullptr && nativeInstance->hasEditor();
    const int rows = effect.numParameters() + (hasPluginButton ? 1 : 0);

    auto* content = new Content(*this, effect, nativeInstance);
    setContentOwned(content, true);
    setResizable(false, false);
    centreWithSize(320, 40 + rows * 24);
    setVisible(true);
}

// The content component (and any open native plugin editor it owns) is torn down automatically
EffectEditorWindow::~EffectEditorWindow() {
}

// Hides the window, the owning strip destroys it on the next structural edit
void EffectEditorWindow::closeButtonPressed() {
    setVisible(false);
}

// Reports right clicks via onRightClick, otherwise behaves like a normal slider
void EffectEditorWindow::ParamSlider::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isPopupMenu()) {
        if (onRightClick) {
            onRightClick();
        }
        return;
    }

    Slider::mouseDown(event);
}

// Builds one row per parameter, plus an Open Plugin Editor button when the plugin has one
EffectEditorWindow::Content::Content(EffectEditorWindow& window, engine::Effect& effect,
                                      plugins::IPluginInstance* nativeInstance)
    : m_window(window)
    , m_effect(effect)
    , m_nativeInstance(nativeInstance)
{
    if (m_nativeInstance != nullptr && m_nativeInstance->hasEditor()) {
        m_openPluginEditorButton = std::make_unique<juce::TextButton>("Open Plugin Editor");
        m_openPluginEditorButton->onClick = [this] {
            if (m_pluginWindow == nullptr) {
                m_pluginWindow = std::make_unique<PluginWindow>(*m_nativeInstance, "Plugin Editor");
            }
            m_pluginWindow->open();
        };
        addAndMakeVisible(*m_openPluginEditorButton);
    }

    const int numParams = m_effect.numParameters();
    for (int i = 0; i < numParams; ++i) {
        ParamRow row;

        row.label = std::make_unique<juce::Label>();
        row.label->setText(m_effect.parameterName(i), juce::dontSendNotification);
        addAndMakeVisible(*row.label);

        row.slider = std::make_unique<ParamSlider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        row.slider->setRange(0.0, 1.0);
        row.slider->setValue(m_effect.getParameter(i), juce::dontSendNotification);
        row.slider->onRightClick = [this, i] {
            showMidiLearnMenu(i);
        };
        juce::Slider* sliderPtr = row.slider.get();
        row.slider->onValueChange = [this, i, sliderPtr] {
            m_effect.setParameter(i, static_cast<float>(sliderPtr->getValue()));
        };
        addAndMakeVisible(*row.slider);

        m_rows.push_back(std::move(row));
    }
}

// Opens the MIDI Learn or Remove MIDI Mapping menu for one parameter
void EffectEditorWindow::Content::showMidiLearnMenu(int paramIndex) {
    const bool mapped = m_window.isParameterMapped && m_window.isParameterMapped(paramIndex);

    juce::PopupMenu menu;
    menu.addItem(1, mapped ? "Remove MIDI Mapping" : "MIDI Learn");

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, paramIndex, mapped](int result) {
        if (result != 1) {
            return;
        }

        if (mapped) {
            if (m_window.onMidiUnlearnRequested) {
                m_window.onMidiUnlearnRequested(paramIndex);
            }
        } else if (m_window.onMidiLearnRequested) {
            m_window.onMidiLearnRequested(paramIndex);
        }
    });
}

// Lays out the optional plugin-editor button, then one row per parameter, top to bottom
void EffectEditorWindow::Content::resized() {
    auto bounds = getLocalBounds().reduced(8);

    if (m_openPluginEditorButton != nullptr) {
        m_openPluginEditorButton->setBounds(bounds.removeFromTop(kRowHeight));
    }

    for (auto& row : m_rows) {
        auto rowArea = bounds.removeFromTop(kRowHeight);
        const int labelWidth = static_cast<int>(static_cast<float>(rowArea.getWidth()) * 0.4f);
        row.label->setBounds(rowArea.removeFromLeft(labelWidth));
        row.slider->setBounds(rowArea);
    }
}

} // namespace howl::ui
