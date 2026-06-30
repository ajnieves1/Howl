// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: owns graph nodes and processes them in topological order

#include "engine/Graph.h"

namespace hearth::engine {

// Takes ownership of the node, returns its id
Graph::NodeId Graph::addNode(std::unique_ptr<Node> node) {
    m_nodes.push_back(std::move(node));
    return m_nodes.size() - 1;
}

// Declares that "from" must process before "to"
void Graph::connect(NodeId from, NodeId to) {
    m_edges.push_back({ from, to });
}

// Computes the topological processing order, call after all addNode() and connect() calls
// Nodes that are part of a cycle never reach in-degree zero and are silently
// left out of the order, since a cycle cannot be processed at all
void Graph::prepare() {
    std::vector<int> inDegree(m_nodes.size(), 0);
    for (const Edge& edge : m_edges) {
        ++inDegree[edge.to];
    }

    std::vector<NodeId> ready;
    for (NodeId id = 0; id < m_nodes.size(); ++id) {
        if (inDegree[id] == 0) {
            ready.push_back(id);
        }
    }

    m_order.clear();
    while (!ready.empty()) {
        const NodeId current = ready.back();
        ready.pop_back();
        m_order.push_back(current);

        for (const Edge& edge : m_edges) {
            if (edge.from == current && --inDegree[edge.to] == 0) {
                ready.push_back(edge.to);
            }
        }
    }
}

// [RT] Processes every node in topological order, in place on audio
void Graph::process(AudioBlock& audio, SampleCount pos) noexcept {
    for (NodeId id : m_order) {
        m_nodes[id]->process(audio, pos);
    }
}

} // namespace hearth::engine
