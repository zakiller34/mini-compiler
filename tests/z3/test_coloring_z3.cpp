#include <gtest/gtest.h>
#include <z3.h>

#include <map>
#include <string>
#include <variant>
#include <vector>

#include "passes/graph_coloring.h"
#include "passes/interference.h"

using namespace mc;

/// @brief Z3 predicate: valid_coloring
/// ForAll (u,v) in edges, color(u) != color(v)
/// Uses Z3 to verify the property universally
static bool z3_valid_coloring(const Graph &graph,
                              const std::map<Location, int> &coloring) {
    Z3_config cfg = Z3_mk_config();
    Z3_context ctx = Z3_mk_context(cfg);
    Z3_del_config(cfg);

    // For each edge, assert color(u) != color(v)
    // If all are satisfiable, coloring is valid
    // invariant: all edges checked so far have distinct colors
    for (const auto &[node, neighbors] : graph.adj) {
        auto it_n = coloring.find(node);
        if (it_n == coloring.end()) {
            Z3_del_context(ctx);
            return false;
        }
        for (const auto &neighbor : neighbors) {
            auto it_nb = coloring.find(neighbor);
            if (it_nb == coloring.end()) {
                Z3_del_context(ctx);
                return false;
            }
            // Create Z3 assertion: color_u == color_v (should be UNSAT)
            Z3_sort int_sort = Z3_mk_int_sort(ctx);
            Z3_ast cu = Z3_mk_int(ctx, it_n->second, int_sort);
            Z3_ast cv = Z3_mk_int(ctx, it_nb->second, int_sort);
            Z3_ast eq = Z3_mk_eq(ctx, cu, cv);

            Z3_solver solver = Z3_mk_solver(ctx);
            Z3_solver_inc_ref(ctx, solver);
            Z3_solver_assert(ctx, solver, eq);
            Z3_lbool result = Z3_solver_check(ctx, solver);
            Z3_solver_dec_ref(ctx, solver);

            if (result == Z3_L_TRUE) {
                // color(u) == color(v) is satisfiable => invalid coloring
                Z3_del_context(ctx);
                return false;
            }
        }
    }

    Z3_del_context(ctx);
    return true;
}

TEST(ColoringZ3, TwoVarsValidColoring) {
    Graph graph;
    Location x{"x"}, y{"y"};
    graph.nodes = {x, y};
    graph.add_edge(x, y);

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(z3_valid_coloring(graph, coloring));
}

TEST(ColoringZ3, CliqueValid) {
    Graph graph;
    std::vector<Location> vars;
    for (int i = 0; i < 5; ++i) {
        vars.push_back(Location{"v" + std::to_string(i)});
        graph.nodes.insert(vars.back());
    }
    for (size_t i = 0; i < vars.size(); ++i) {
        for (size_t j = i + 1; j < vars.size(); ++j) {
            graph.add_edge(vars[i], vars[j]);
        }
    }

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(z3_valid_coloring(graph, coloring));
}

TEST(ColoringZ3, SpillCaseValid) {
    // K12 clique: forces spills, still valid
    Graph graph;
    std::vector<Location> vars;
    for (int i = 0; i < 12; ++i) {
        vars.push_back(Location{"v" + std::to_string(i)});
        graph.nodes.insert(vars.back());
    }
    for (size_t i = 0; i < vars.size(); ++i) {
        for (size_t j = i + 1; j < vars.size(); ++j) {
            graph.add_edge(vars[i], vars[j]);
        }
    }

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(z3_valid_coloring(graph, coloring));
}

TEST(ColoringZ3, WithPrecoloredRegsValid) {
    Graph graph;
    Location x{"x"}, rcx{x86::Reg::Rcx}, rdx{x86::Reg::Rdx};
    graph.nodes = {x, rcx, rdx};
    graph.add_edge(x, rcx);
    graph.add_edge(x, rdx);
    graph.add_edge(rcx, rdx);

    auto coloring = color_graph(graph, num_allocable_regs());
    EXPECT_TRUE(z3_valid_coloring(graph, coloring));
}
