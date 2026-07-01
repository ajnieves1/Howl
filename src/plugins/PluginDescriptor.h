// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: metadata describing one discovered plugin

#pragma once

#include <string>

namespace hearth::plugins {

struct PluginDescriptor {
    std::string name;
    std::string vendor;
    std::string format;
    std::string path;
    bool isInstrument;
};

} // namespace hearth::plugins
