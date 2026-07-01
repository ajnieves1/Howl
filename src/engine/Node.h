// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: the interface every audio graph node implements

#pragma once

#include "core/Types.h"

namespace howl::engine {

class Node {
public:
    // Allows deleting through a Node pointer
    virtual ~Node() = default;

    // [RT] Processes the block in place at the given timeline position
    virtual void process(AudioBlock& audio, SampleCount pos) noexcept = 0;
};

} // namespace howl::engine
