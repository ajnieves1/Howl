// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: owns graph nodes and processes them in topological order

#pragma once

#include "core/Types.h"
#include "engine/Node.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace howl::engine {

class Graph {
public:
    using NodeId = std::size_t;

    // Takes ownership of the node, returns its id
    NodeId addNode(std::unique_ptr<Node> node);

    // Declares that "from" must process before "to"
    void connect(NodeId from, NodeId to);

    // Computes the topological processing order, call after all addNode() and connect() calls
    void prepare();

    // [RT] Processes every node in topological order, in place on audio
    void process(AudioBlock& audio, SampleCount pos) noexcept;

private:
    struct Edge {
        NodeId from;
        NodeId to;
    };

    std::vector<std::unique_ptr<Node>> m_nodes;
    std::vector<Edge> m_edges;
    std::vector<NodeId> m_order;
};

} // namespace howl::engine
