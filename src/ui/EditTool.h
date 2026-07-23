// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the tool that drives mouse edits in the timeline and the piano roll

#pragma once

namespace howl::ui {

// The selected edit tool. Draw is the default and behaves exactly as the editors always have,
// so a user who never touches the palette sees no change
enum class EditTool {
    Draw,
    Paint,
    Delete,
    Mute,
    Slice,
    Select
};

} // namespace howl::ui
