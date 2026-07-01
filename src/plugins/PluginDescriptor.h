// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: metadata describing one discovered plugin

#pragma once

#include <string>

namespace howl::plugins {

struct PluginDescriptor {
    std::string name;
    std::string vendor;
    std::string format;
    std::string path;
    bool isInstrument;
};

} // namespace howl::plugins
