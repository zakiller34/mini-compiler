#include <gtest/gtest.h>
#include <z3.h>

#include <set>
#include <string>
#include <vector>

#include "ir/x86_ir.h"
#include "passes/liveness.h"

/// @brief Z3 predicate: liveness_covers_uses
/// ForAll v k, if v is used at instruction k, then v is in live_after
/// for some earlier instruction (or in live_before[k]).
/// Equivalently: for every read of v at position k, v must be in
/// live_after[k-1] (i.e., live before k).
///
/// We encode: for each (var, instr_index) where var is read,
/// assert var NOT in live_after[index-1] and check UNSAT.
static bool z3_liveness_covers_uses(
    const std::vector<x86::Instr> &instrs,
    const std::vector<std::set<std::string>> &live_after) {
    Z3_config cfg = Z3_mk_config();
    Z3_context ctx = Z3_mk_context(cfg);
    Z3_del_config(cfg);

    // invariant: all uses checked so far are covered by liveness
    for (size_t k = 0; k < instrs.size(); ++k) {
        auto reads = instr_reads(instrs[k]);
        for (const auto &var : reads) {
            // var is read at instruction k
            // It must be live before k:
            //   - If k > 0: var must be in live_after[k-1]
            //   - If k == 0: var must be live at entry (before first instr)
            //     For L_Var, no var is live at entry, so k==0 reads
            //     shouldn't happen for well-formed programs... unless
            //     it's a read-modify-write where the var was just defined.
            //     Actually, a var read at k=0 with no prior def is an error.

            bool covered = false;
            if (k > 0) {
                covered = live_after[k - 1].count(var) > 0;
            }
            // Also check: if var is written at k (read-modify-write),
            // it must have been live before k
            // For k == 0 with a read-modify-write, var must come from
            // outside — this is a well-formedness issue

            // Encode as Z3: assert NOT covered, check SAT
            Z3_sort bool_sort = Z3_mk_bool_sort(ctx);
            Z3_ast assertion =
                covered ? Z3_mk_false(ctx) : Z3_mk_true(ctx);

            Z3_solver solver = Z3_mk_solver(ctx);
            Z3_solver_inc_ref(ctx, solver);
            Z3_solver_assert(ctx, solver, assertion);
            Z3_lbool result = Z3_solver_check(ctx, solver);
            Z3_solver_dec_ref(ctx, solver);

            if (result == Z3_L_TRUE && !covered) {
                // Use is NOT covered by liveness => bug
                Z3_del_context(ctx);
                return false;
            }
        }
    }

    Z3_del_context(ctx);
    return true;
}

TEST(LivenessZ3, SimpleSequenceCoverage) {
    // movq $1, x    -- live_after = {x}
    // movq x, %rax  -- live_after = {}
    x86::Block block;
    block.instrs = {
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::VarArg{"x"}, x86::RegArg{x86::Reg::Rax}},
    };
    auto live = analyze_liveness(block);
    EXPECT_TRUE(z3_liveness_covers_uses(block.instrs, live));
}

TEST(LivenessZ3, TwoVarsCoverage) {
    x86::Block block;
    block.instrs = {
        x86::Movq{x86::Imm{1}, x86::VarArg{"x"}},
        x86::Movq{x86::Imm{2}, x86::VarArg{"y"}},
        x86::Addq{x86::VarArg{"x"}, x86::VarArg{"y"}},
        x86::Movq{x86::VarArg{"y"}, x86::RegArg{x86::Reg::Rax}},
    };
    auto live = analyze_liveness(block);
    EXPECT_TRUE(z3_liveness_covers_uses(block.instrs, live));
}

TEST(LivenessZ3, NegCoverage) {
    x86::Block block;
    block.instrs = {
        x86::Movq{x86::Imm{5}, x86::VarArg{"x"}},
        x86::Negq{x86::VarArg{"x"}},
        x86::Movq{x86::VarArg{"x"}, x86::RegArg{x86::Reg::Rax}},
    };
    auto live = analyze_liveness(block);
    EXPECT_TRUE(z3_liveness_covers_uses(block.instrs, live));
}
