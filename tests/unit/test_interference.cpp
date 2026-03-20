#include <gtest/gtest.h>

#include <set>
#include <string>
#include <variant>

#include "ir/x86_ir.h"
#include "passes/interference.h"
#include "passes/liveness.h"

using namespace mc;

/// Helper: build block, run liveness, build interference
static Graph build_graph_from(std::vector<x86::Instr> instrs) {
    x86::Block block{std::move(instrs)};
    auto live = analyze_liveness(block);
    return build_interference(block.instrs, live);
}

TEST(Interference, TwoOverlappingVars) {
    // movq $1, x     -- live_after = {x}
    // movq $2, y     -- live_after = {x, y}
    // addq x, y      -- live_after = {y}
    // movq y, %rax   -- live_after = {}
    auto graph = build_graph_from({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::Imm{2}, x86::VarArg{"y"}},
        x86::Addq{x86::VarArg{"x"}, x86::VarArg{"y"}},
        x86::Movq{x86::VarArg{"y"}, x86::RegArg{x86::Reg::Rax}},
    });
    // x and y are simultaneously live after instr 1 => interference edge
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{"y"}));
}

TEST(Interference, NonOverlappingVarsNoEdge) {
    // movq $1, x     -- live_after = {}  (x dead immediately)
    // movq $2, y     -- live_after = {y}
    // movq y, %rax   -- live_after = {}
    auto graph = build_graph_from({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::Imm{2}, x86::VarArg{"y"}},
        x86::Movq{x86::VarArg{"y"}, x86::RegArg{x86::Reg::Rax}},
    });
    EXPECT_FALSE(graph.has_edge(Location{"x"}, Location{"y"}));
}

TEST(Interference, MovqSpecialCase) {
    // movq x, y — no interference edge between x and y (move-related)
    // but a move_edge should be recorded
    // Setup: x is defined earlier, used later
    // movq $1, x     -- live_after = {x}
    // movq x, y      -- live_after = {y} (x dies, y born from x — no edge)
    // movq y, %rax   -- live_after = {}
    auto graph = build_graph_from({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::VarArg{"x"}, x86::VarArg{"y"}},
        x86::Movq{x86::VarArg{"y"}, x86::RegArg{x86::Reg::Rax}},
    });
    // x and y should NOT have interference edge (movq special case)
    EXPECT_FALSE(graph.has_edge(Location{"x"}, Location{"y"}));
    // But move_edge should exist
    bool has_move = graph.move_edges.count({Location{"x"}, Location{"y"}}) > 0 ||
                    graph.move_edges.count({Location{"y"}, Location{"x"}}) > 0;
    EXPECT_TRUE(has_move);
}

TEST(Interference, CallqClobbersCallerSaved) {
    // movq $1, x         -- live_after = {x}
    // callq read_int     -- live_after = {x} (x still live across call)
    // movq %rax, y       -- live_after = {x, y}
    // addq x, y          -- live_after = {y}
    // movq y, %rax       -- live_after = {}
    auto graph = build_graph_from({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Callq{"read_int", 0},
        x86::Movq{x86::RegArg{x86::Reg::Rax}, x86::VarArg{"y"}},
        x86::Addq{x86::VarArg{"x"}, x86::VarArg{"y"}},
        x86::Movq{x86::VarArg{"y"}, x86::RegArg{x86::Reg::Rax}},
    });
    // x is live across callq, so x interferes with all caller-saved regs
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::Rax}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::Rcx}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::Rdx}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::Rsi}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::Rdi}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::R8}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::R9}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::R10}));
    EXPECT_TRUE(graph.has_edge(Location{"x"}, Location{x86::Reg::R11}));
}

TEST(Interference, NoSelfEdges) {
    auto graph = build_graph_from({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Negq{x86::VarArg{"x"}},
        x86::Movq{x86::VarArg{"x"}, x86::RegArg{x86::Reg::Rax}},
    });
    EXPECT_FALSE(graph.has_edge(Location{"x"}, Location{"x"}));
}
