#include <gtest/gtest.h>

#include <set>
#include <string>

#include "ir/x86_ir.h"
#include "passes/liveness.h"

/// Helper: build a block from instructions
static x86::Block make_block(std::vector<x86::Instr> instrs) {
    return x86::Block{std::move(instrs)};
}

TEST(Liveness, EmptyBlock) {
    auto block = make_block({});
    auto live = analyze_liveness(block);
    EXPECT_TRUE(live.empty());
}

TEST(Liveness, SingleMovImm) {
    // movq $1, x — x is written, not read. live_after[0] = {}
    auto block = make_block({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
    });
    auto live = analyze_liveness(block);
    ASSERT_EQ(live.size(), 1);
    EXPECT_TRUE(live[0].empty());
}

TEST(Liveness, MovThenUse) {
    // movq $1, x    -- live_after[0] = {x}
    // movq x, %rax  -- live_after[1] = {}
    auto block = make_block({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::VarArg{"x"}, x86::RegArg{x86::Reg::Rax}},
    });
    auto live = analyze_liveness(block);
    ASSERT_EQ(live.size(), 2);
    EXPECT_EQ(live[0], (std::set<std::string>{"x"}));
    EXPECT_TRUE(live[1].empty());
}

TEST(Liveness, TwoVarsOverlapping) {
    // movq $1, x     -- live_after = {x}
    // movq $2, y     -- live_after = {x, y}
    // addq x, y      -- live_after = {y}
    // movq y, %rax   -- live_after = {}
    auto block = make_block({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::Imm{2}, x86::VarArg{"y"}},
        x86::Addq{x86::VarArg{"x"}, x86::VarArg{"y"}},
        x86::Movq{x86::VarArg{"y"}, x86::RegArg{x86::Reg::Rax}},
    });
    auto live = analyze_liveness(block);
    ASSERT_EQ(live.size(), 4);
    EXPECT_EQ(live[0], (std::set<std::string>{"x"}));
    EXPECT_EQ(live[1], (std::set<std::string>{"x", "y"}));
    EXPECT_EQ(live[2], (std::set<std::string>{"y"}));
    EXPECT_TRUE(live[3].empty());
}

TEST(Liveness, NegqReadsAndWrites) {
    // movq $5, x     -- live_after = {x}
    // negq x         -- live_after = {x}  (x read and written)
    // movq x, %rax   -- live_after = {}
    auto block = make_block({
        x86::Movq{x86::Imm{5}, x86::VarArg{"x"}},
        x86::Negq{x86::VarArg{"x"}},
        x86::Movq{x86::VarArg{"x"}, x86::RegArg{x86::Reg::Rax}},
    });
    auto live = analyze_liveness(block);
    ASSERT_EQ(live.size(), 3);
    EXPECT_EQ(live[0], (std::set<std::string>{"x"}));
    EXPECT_EQ(live[1], (std::set<std::string>{"x"}));
    EXPECT_TRUE(live[2].empty());
}

TEST(Liveness, DeadVarNotLive) {
    // movq $1, x   -- live_after = {}
    // movq $2, y   -- live_after = {y}
    // movq y, %rax -- live_after = {}
    // x is dead after instruction 0 (never used again)
    auto block = make_block({
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::Imm{2}, x86::VarArg{"y"}},
        x86::Movq{x86::VarArg{"y"}, x86::RegArg{x86::Reg::Rax}},
    });
    auto live = analyze_liveness(block);
    ASSERT_EQ(live.size(), 3);
    EXPECT_TRUE(live[0].empty()); // x is dead (never used)
    EXPECT_EQ(live[1], (std::set<std::string>{"y"}));
    EXPECT_TRUE(live[2].empty());
}

TEST(Liveness, CallqDoesNotWriteVars) {
    // callq read_int   -- live_after = {} (callq writes no vars)
    // movq %rax, x     -- live_after = {x}
    // movq x, %rax     -- live_after = {}
    auto block = make_block({
        x86::Callq{"read_int", 0},
        x86::Movq{x86::RegArg{x86::Reg::Rax}, x86::VarArg{"x"}},
        x86::Movq{x86::VarArg{"x"}, x86::RegArg{x86::Reg::Rax}},
    });
    auto live = analyze_liveness(block);
    ASSERT_EQ(live.size(), 3);
    EXPECT_TRUE(live[0].empty());
    EXPECT_EQ(live[1], (std::set<std::string>{"x"}));
    EXPECT_TRUE(live[2].empty());
}

TEST(Liveness, InstrReadsWriteHelpers) {
    // Test reads/writes helpers directly
    auto movq = x86::Movq{x86::VarArg{"x"}, x86::VarArg{"y"}};
    x86::Instr instr = movq;
    auto r = instr_reads(instr);
    auto w = instr_writes(instr);
    EXPECT_EQ(r, (std::set<std::string>{"x"}));
    EXPECT_EQ(w, (std::set<std::string>{"y"}));

    // addq reads both src and dst
    auto addq = x86::Addq{x86::VarArg{"a"}, x86::VarArg{"b"}};
    x86::Instr instr2 = addq;
    auto r2 = instr_reads(instr2);
    auto w2 = instr_writes(instr2);
    EXPECT_EQ(r2, (std::set<std::string>{"a", "b"}));
    EXPECT_EQ(w2, (std::set<std::string>{"b"}));
}
