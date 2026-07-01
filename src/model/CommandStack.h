// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: undo and redo stacks of reversible commands

#pragma once

#include "model/Command.h"

#include <memory>
#include <vector>

namespace howl::model {

class CommandStack {
public:
    // Executes the command and pushes it onto the undo stack, clears the redo stack
    void perform(std::unique_ptr<Command> command);

    // Returns true if undo() would do something
    bool canUndo() const;

    // Returns true if redo() would do something
    bool canRedo() const;

    // Undoes the most recently performed or redone command
    void undo();

    // Re-applies the most recently undone command
    void redo();

private:
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
};

} // namespace howl::model
