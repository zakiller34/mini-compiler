#include "graph_coloring.h"

#include <algorithm>
#include <limits>

namespace {

/// @brief Compute saturation of a node (number of distinct colors among
/// neighbors)
/// @requires node is in graph, coloring has colors for colored neighbors
/// @ensures result = |{coloring[n] : n in adj[node] and n in coloring}|
int saturation(const Graph &graph, const Location &node,
               const std::map<Location, int> &coloring) {
    std::set<int> neighbor_colors;
    auto adj_it = graph.adj.find(node);
    if (adj_it == graph.adj.end()) {
        return 0;
    }
    // invariant: neighbor_colors has colors of checked neighbors
    for (const auto &neighbor : adj_it->second) {
        auto c_it = coloring.find(neighbor);
        if (c_it != coloring.end()) {
            neighbor_colors.insert(c_it->second);
        }
    }
    return static_cast<int>(neighbor_colors.size());
}

/// @brief Degree of a node in the graph
int degree(const Graph &graph, const Location &node) {
    auto it = graph.adj.find(node);
    if (it == graph.adj.end()) {
        return 0;
    }
    return static_cast<int>(it->second.size());
}

/// @brief Find lowest color not used by neighbors
/// @ensures result >= 0 and result not in {coloring[n] : n in adj[node]}
int lowest_available_color(const Graph &graph, const Location &node,
                           const std::map<Location, int> &coloring) {
    std::set<int> used;
    auto adj_it = graph.adj.find(node);
    if (adj_it != graph.adj.end()) {
        for (const auto &neighbor : adj_it->second) {
            auto c_it = coloring.find(neighbor);
            if (c_it != coloring.end()) {
                used.insert(c_it->second);
            }
        }
    }

    int color = 0;
    // invariant: colors [0..color) are all in used
    // decreases: will find gap or exceed used.size()
    while (used.count(color) > 0) {
        ++color;
    }
    return color;
}

/// @brief Find move-biased color: prefer color of a move-related neighbor
/// @ensures returns a valid color not used by neighbors, or -1 if no bias
int move_biased_color(const Graph &graph, const Location &node,
                      const std::map<Location, int> &coloring) {
    std::set<int> used_by_neighbors;
    auto adj_it = graph.adj.find(node);
    if (adj_it != graph.adj.end()) {
        for (const auto &neighbor : adj_it->second) {
            auto c_it = coloring.find(neighbor);
            if (c_it != coloring.end()) {
                used_by_neighbors.insert(c_it->second);
            }
        }
    }

    // Check move-related neighbors for a color we can reuse
    // invariant: checked move_edges so far, none yielded valid bias
    for (const auto &[a, b] : graph.move_edges) {
        Location partner;
        if (a == node) {
            partner = b;
        } else if (b == node) {
            partner = a;
        } else {
            continue;
        }
        auto c_it = coloring.find(partner);
        if (c_it != coloring.end() &&
            used_by_neighbors.count(c_it->second) == 0) {
            return c_it->second;
        }
    }
    return -1; // no bias found
}

} // namespace

const std::vector<x86::Reg> &allocable_regs() {
    static const std::vector<x86::Reg> regs = {
        x86::Reg::Rcx, x86::Reg::Rdx, x86::Reg::Rsi, x86::Reg::Rdi,
        x86::Reg::R8,  x86::Reg::R9,  x86::Reg::R10, x86::Reg::Rbx,
        x86::Reg::R12, x86::Reg::R13, x86::Reg::R14};
    return regs;
}

int num_allocable_regs() { return static_cast<int>(allocable_regs().size()); }

x86::Reg color_to_reg(int color) { return allocable_regs().at(color); }

int reg_to_color(x86::Reg r) {
    const auto &regs = allocable_regs();
    // invariant: checked regs[0..i), none matched
    for (size_t i = 0; i < regs.size(); ++i) {
        if (regs[i] == r) {
            return static_cast<int>(i);
        }
    }
    return -1; // not allocable
}

/// @brief Color graph using DSATUR with move biasing
/// @requires graph is valid undirected graph
/// @ensures no two adjacent nodes share a color
/// @ensures Reg nodes keep pre-assigned colors
std::map<Location, int> color_graph(const Graph &graph, int num_regs) {
    std::map<Location, int> coloring;
    std::set<Location> uncolored;

    // Pre-color register nodes
    // invariant: all Reg nodes seen so far are pre-colored
    for (const auto &node : graph.nodes) {
        if (const auto *reg = std::get_if<x86::Reg>(&node)) {
            int c = reg_to_color(*reg);
            if (c >= 0) {
                coloring[node] = c;
            } else {
                // Non-allocable reg (rax, rsp, rbp, r11, r15): assign
                // unique negative color so it doesn't conflict with allocable
                // colors
                coloring[node] = -(static_cast<int>(coloring.size()) + 1);
            }
        } else {
            uncolored.insert(node);
        }
    }

    // DSATUR: repeatedly pick uncolored node with max saturation, break ties
    // by degree
    // invariant: |uncolored| decreases each iteration
    // decreases: uncolored.size()
    while (!uncolored.empty()) {
        // Find node with max saturation (ties broken by max degree)
        Location best = *uncolored.begin();
        int best_sat = -1;
        int best_deg = -1;

        // invariant: best is the best candidate among checked nodes
        for (const auto &node : uncolored) {
            int sat = saturation(graph, node, coloring);
            int deg = degree(graph, node);
            if (sat > best_sat || (sat == best_sat && deg > best_deg)) {
                best = node;
                best_sat = sat;
                best_deg = deg;
            }
        }

        // Try move biasing first
        int color = move_biased_color(graph, best, coloring);
        if (color < 0) {
            color = lowest_available_color(graph, best, coloring);
        }

        coloring[best] = color;
        uncolored.erase(best);
    }

    return coloring;
}
