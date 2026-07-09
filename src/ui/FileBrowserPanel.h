// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: folder tree of wav samples, click to preview and drag out

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace howl::ui {

// Folder tree of .wav samples with click to preview and drag out. Shows a hint
// label instead of the tree until a root folder that actually exists is set
class FileBrowserPanel : public juce::Component, private juce::FileBrowserListener {
public:
    // Builds the tree against the given root, a nonexistent root shows a hint label
    explicit FileBrowserPanel(const juce::File& initialRoot);

    // Stops the background scanning thread
    ~FileBrowserPanel() override;

    // Fired when the user picks a new root folder, the app persists it
    std::function<void(juce::File)> onRootChanged;

    // Fired when a .wav file is clicked, the app starts a preview
    std::function<void(juce::File)> onFileClicked;

    // Returns the file currently selected in the tree, for drop targets
    juce::File selectedFile() const;

    // Lays out the set folder button above the hint label or tree
    void resized() override;

private:
    // FileBrowserListener: fires onFileClicked for an actual file, ignores directories
    void selectionChanged() override;
    void fileClicked(const juce::File& file, const juce::MouseEvent& event) override;
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File& newRoot) override;

    // Re-roots the tree at root, showing the hint label instead when it does not exist
    void setRoot(const juce::File& root);

    // Opens an async directory chooser, re-roots and fires onRootChanged on pick
    void showSetFolderChooser();

    juce::TimeSliceThread m_scanThread { "FileBrowserPanel scan" };
    juce::WildcardFileFilter m_filter;
    std::unique_ptr<juce::DirectoryContentsList> m_contentsList;
    std::unique_ptr<juce::FileTreeComponent> m_tree;
    juce::Label m_hintLabel;
    juce::TextButton m_setFolderButton { "Set Folder..." };
};

} // namespace howl::ui
