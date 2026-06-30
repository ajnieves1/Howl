// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: Graph and Node processing-order tests

#include "engine/Graph.h"
#include "engine/Node.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using hearth::AudioBlock;
using hearth::SampleCount;
using hearth::engine::Graph;
using hearth::engine::Node;

namespace {

// Test fixture: multiplies every sample by a fixed gain
class GainNode : public Node {
public:
    explicit GainNode(float gain)
        : m_gain(gain)
    {
    }

    // Scales every channel of the block by m_gain
    void process(AudioBlock& audio, SampleCount) noexcept override {
        for (int channel = 0; channel < audio.numChannels; ++channel) {
            for (int frame = 0; frame < audio.numFrames; ++frame) {
                audio.channels[channel][frame] *= m_gain;
            }
        }
    }

private:
    float m_gain;
};

} // namespace

TEST_CASE("GainNode scales every sample by its gain, sample-exact", "[graph]") {
    float samples[4] = { 1.0f, 2.0f, -1.0f, 0.5f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 4 };

    GainNode gain(2.0f);
    gain.process(block, 0);

    REQUIRE(samples[0] == 2.0f);
    REQUIRE(samples[1] == 4.0f);
    REQUIRE(samples[2] == -2.0f);
    REQUIRE(samples[3] == 1.0f);
}

TEST_CASE("Graph processes connected nodes in topological order", "[graph]") {
    float samples[2] = { 1.0f, 1.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 2 };

    Graph graph;
    const Graph::NodeId first = graph.addNode(std::make_unique<GainNode>(2.0f));
    const Graph::NodeId second = graph.addNode(std::make_unique<GainNode>(3.0f));
    graph.connect(first, second);
    graph.prepare();

    graph.process(block, 0);

    REQUIRE(samples[0] == 6.0f);
    REQUIRE(samples[1] == 6.0f);
}

TEST_CASE("Graph processes an unconnected node without crashing", "[graph]") {
    float samples[2] = { 2.0f, 2.0f };
    float* channels[1] = { samples };
    AudioBlock block { channels, 1, 2 };

    Graph graph;
    graph.addNode(std::make_unique<GainNode>(5.0f));
    graph.prepare();

    graph.process(block, 0);

    REQUIRE(samples[0] == 10.0f);
    REQUIRE(samples[1] == 10.0f);
}
