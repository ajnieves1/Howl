// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: renders an Arrangement faster than real time to a wav file

#pragma once

#include "core/Types.h"
#include "engine/Transport.h"
#include "model/Arrangement.h"

#include <string>

namespace howl::model {

class OfflineRenderer {
public:
    // Renders arrangement through transport for numFrames, faster than real
    // time, block by block, and writes the result to a wav at path, starts
    // the transport playing and returns false if the file could not be opened
    static bool renderToFile(Arrangement& arrangement, engine::Transport& transport, double sampleRate,
                              int blockSize, int numChannels, SampleCount numFrames, const std::string& path);
};

} // namespace howl::model
