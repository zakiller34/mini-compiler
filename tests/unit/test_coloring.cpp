#include <gtest/gtest.h>

#include <map>
#include <string>
#include <variant>

#include "ir/x86_ir.h"
#include "passes/graph_coloring.h"
#include "passes/interference.h"

using namespace mc;

/// @brief Validate coloring: no two adjacent nodes share a color
static bool valid_coloring(const Graph &graph,
                           const std::map<Location, int> &coloring) {
    // invariant: all checked edges had distinct colors
    for (const auto &[node, neighbors] : graph.adj) {
        auto it_n = coloring.find(node);
        if (it_n == coloring.end()) {
            return false; // uncolored node
        }
        for (const auto &neighbor : neighbors) {
            auto it_nb = coloring.find(neighbor);
            if (it_nb == coloring.end()) {
                return false;
            }
            if (it_n->second == it_nb->second) {
                return false; // adjacent same color
            }
        }
    }
    return true;
}

TEST(GraphColoring, TwoAdjacentVars) {
    Graph graph;
    Location x{"x"};
    Location y{"y"};
    graph.nodes = {x, y};
    graph.add_edge(x, y);

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(valid_coloring(graph, coloring));
    EXPECT_NE(coloring[x], coloring[y]);
}

TEST(GraphColoring, ThreeClique) {
    Graph graph;
    Location a{"a"}, b{"b"}, c{"c"};
    graph.nodes = {a, b, c};
    graph.add_edge(a, b);
    graph.add_edge(b, c);
    graph.add_edge(a, c);

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(valid_coloring(graph, coloring));
    // 3 distinct colors needed
    std::set<int> colors = {coloring[a], coloring[b], coloring[c]};
    EXPECT_EQ(colors.size(), 3);
}

TEST(GraphColoring, PrecoloredRespected) {
    Graph graph;
    Location x{"x"};
    Location rcx{x86::Reg::Rcx}; // pre-colored to 0
    graph.nodes = {x, rcx};
    graph.add_edge(x, rcx);

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(valid_coloring(graph, coloring));
    EXPECT_EQ(coloring[rcx], reg_to_color(x86::Reg::Rcx));
    EXPECT_NE(coloring[x], coloring[rcx]);
}

TEST(GraphColoring, SpillCase) {
    // Create 12 vars all interfering with each other (complete graph K12)
    // Only 11 regs => at least 1 spill (color >= 11)
    Graph graph;
    std::vector<Location> vars;
    for (int i = 0; i < 12; ++i) {
        vars.push_back(Location{"v" + std::to_string(i)});
        graph.nodes.insert(vars.back());
    }
    // invariant: edges added for pairs [0..i) x [0..j)
    for (size_t i = 0; i < vars.size(); ++i) {
        for (size_t j = i + 1; j < vars.size(); ++j) {
            graph.add_edge(vars[i], vars[j]);
        }
    }

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(valid_coloring(graph, coloring));

    // At least one var must have color >= 11 (spilled)
    bool has_spill = false;
    for (const auto &v : vars) {
        if (coloring[v] >= num_allocable_regs()) {
            has_spill = true;
        }
    }
    EXPECT_TRUE(has_spill);
}

TEST(GraphColoring, MoveBiasPrefersSameColor) {
    // x -> y via movq. If they don't interfere, move biasing should give
    // them the same color.
    Graph graph;
    Location x{"x"}, y{"y"};
    graph.nodes = {x, y};
    // No interference edge, but a move edge
    graph.add_move_edge(x, y);

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(valid_coloring(graph, coloring));
    EXPECT_EQ(coloring[x], coloring[y]); // move bias should unify
}

TEST(GraphColoring, SingleIsolatedVar) {
    Graph graph;
    Location x{"x"};
    graph.nodes = {x};

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_EQ(coloring.count(x), 1);
    EXPECT_GE(coloring[x], 0);
    EXPECT_LT(coloring[x], num_allocable_regs());
}

TEST(GraphColoring, ColorToRegRoundtrip) {
    for (int i = 0; i < num_allocable_regs(); ++i) {
        auto reg = color_to_reg(i);
        EXPECT_EQ(reg_to_color(reg), i);
    }
}
