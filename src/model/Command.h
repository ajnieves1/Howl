// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the interface every reversible arrangement edit implements

#pragma once

namespace howl::model {

class Command {
public:
    // Allows deleting through a Command pointer
    virtual ~Command() = default;

    // Applies the edit
    virtual void execute() = 0;

    // Reverses the edit
    virtual void undo() = 0;
};

} // namespace howl::model
