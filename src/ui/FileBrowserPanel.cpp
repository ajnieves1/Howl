// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: folder tree of wav samples, click to preview and drag out

#include "ui/FileBrowserPanel.h"

#include "ui/Theme.h"

namespace howl::ui {

// Starts the scan thread, builds the tree over the given root, and shows the
// set folder button above whichever of the hint label or the tree applies
FileBrowserPanel::FileBrowserPanel(const juce::File& initialRoot)
    : m_filter("*.wav", "*", "wav files")
{
    m_scanThread.startThread();

    m_contentsList = std::make_unique<juce::DirectoryContentsList>(&m_filter, m_scanThread);

    m_tree = std::make_unique<juce::FileTreeComponent>(*m_contentsList);
    m_tree->addListener(this);
    m_tree->setDragAndDropDescription("howl-sample");
    addChildComponent(*m_tree);

    m_hintLabel.setText("Set a folder with the button above", juce::dontSendNotification);
    m_hintLabel.setJustificationType(juce::Justification::centred);
    m_hintLabel.setColour(juce::Label::textColourId, theme::kTextSecondary);
    addChildComponent(m_hintLabel);

    m_setFolderButton.onClick = [this] {
        showSetFolderChooser();
    };
    addAndMakeVisible(m_setFolderButton);

    setRoot(initialRoot);
}

// Stops the background scanning thread
FileBrowserPanel::~FileBrowserPanel() {
    m_tree->removeListener(this);
    m_scanThread.stopThread(1000);
}

// Returns the file currently selected in the tree, for drop targets
juce::File FileBrowserPanel::selectedFile() const {
    return m_tree->getSelectedFile();
}

// Lays out the set folder button above the hint label or tree
void FileBrowserPanel::resized() {
    auto bounds = getLocalBounds();
    m_setFolderButton.setBounds(bounds.removeFromTop(24).reduced(2));
    m_hintLabel.setBounds(bounds);
    m_tree->setBounds(bounds);
}

// FileBrowserListener: selection alone carries no action, clicking does
void FileBrowserPanel::selectionChanged() {
}

// Fires onFileClicked for an actual file, ignores directory clicks
void FileBrowserPanel::fileClicked(const juce::File& file, const juce::MouseEvent&) {
    if (file.existsAsFile() && onFileClicked) {
        onFileClicked(file);
    }
}

// Double clicks are not this panel's business, a single click already previews
void FileBrowserPanel::fileDoubleClicked(const juce::File&) {
}

// The tree re-roots itself, nothing further to do here
void FileBrowserPanel::browserRootChanged(const juce::File&) {
}

// Re-roots the tree at root, showing the hint label instead when it does not exist
void FileBrowserPanel::setRoot(const juce::File& root) {
    const bool valid = root.isDirectory();
    m_hintLabel.setVisible(!valid);
    m_tree->setVisible(valid);

    if (valid) {
        m_contentsList->setDirectory(root, true, true);
    }
}

// Opens an async directory chooser, re-roots and fires onRootChanged on pick
void FileBrowserPanel::showSetFolderChooser() {
    auto chooser = std::make_shared<juce::FileChooser>("Select a sample folder", m_contentsList->getDirectory());
    constexpr int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        const juce::File result = fc.getResult();
        if (result == juce::File()) {
            return;
        }

        setRoot(result);

        if (onRootChanged) {
            onRootChanged(result);
        }
    });
}

} // namespace howl::ui
