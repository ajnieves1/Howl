// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: undo and redo stacks of reversible commands

#include "model/CommandStack.h"

namespace howl::model {

// Executes the command and pushes it onto the undo stack, clears the redo stack
void CommandStack::perform(std::unique_ptr<Command> command) {
    command->execute();
    m_undoStack.push_back(std::move(command));
    m_redoStack.clear();
    ++m_changeCounter;
}

// Returns true if undo() would do something
bool CommandStack::canUndo() const {
    return !m_undoStack.empty();
}

// Returns true if redo() would do something
bool CommandStack::canRedo() const {
    return !m_redoStack.empty();
}

// Undoes the most recently performed or redone command
void CommandStack::undo() {
    if (m_undoStack.empty()) {
        return;
    }

    std::unique_ptr<Command> command = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    command->undo();
    m_redoStack.push_back(std::move(command));
    ++m_changeCounter;
}

// Re-applies the most recently undone command
void CommandStack::redo() {
    if (m_redoStack.empty()) {
        return;
    }

    std::unique_ptr<Command> command = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    command->execute();
    m_undoStack.push_back(std::move(command));
    ++m_changeCounter;
}

// Empties both stacks without undoing anything, for loading a fresh project
void CommandStack::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}

// Returns the running change counter
std::uint64_t CommandStack::changeCounter() const {
    return m_changeCounter;
}

} // namespace howl::model
