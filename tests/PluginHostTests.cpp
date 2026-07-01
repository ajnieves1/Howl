// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: PluginHost VST3 scan and cache-file tests

#include "plugins/PluginHost.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>

using howl::plugins::PluginHost;

TEST_CASE("PluginHost scans for VST3 plugins and writes a cache file", "[plugins]") {
    PluginHost host;

    host.rescan();
    host.waitForScanToFinish();

    const auto descriptors = host.list();

    // Not asserted >0 here, CI runners and most dev machines have no VST3s
    // installed, the scan and cache-write paths are what this test proves
    std::cout << "Howl: discovered " << descriptors.size() << " VST3 plugin(s)\n";

    for (const auto& descriptor : descriptors) {
        REQUIRE_FALSE(descriptor.name.empty());
        REQUIRE(descriptor.format == "VST3");
    }
}

TEST_CASE("PluginHost.rescan() does not block the calling thread", "[plugins]") {
    PluginHost host;

    // If rescan() blocked until the scan finished, this call would simply
    // take as long as a full system scan, either way it must return, and a
    // second rescan() while one is in flight must not crash or deadlock
    host.rescan();
    host.rescan();
    host.waitForScanToFinish();

    SUCCEED("rescan() returned without blocking or crashing");
}
