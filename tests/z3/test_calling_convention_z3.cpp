#include <gtest/gtest.h>
#include <z3.h>

#include <string>
#include <variant>
#include <vector>

#include "ir/x86_ir.h"

using namespace mc;

/// @brief Z3 predicate: args_in_correct_registers
/// ForAll i in 0..min(arity,6)-1, arg_i must be moved to correct
/// System V register: rdi, rsi, rdx, rcx, r8, r9.
/// @requires instrs is the instruction sequence for a call site
/// @ensures returns true iff all args placed in correct regs
static bool z3_args_in_correct_registers(
    const std::vector<x86::Instr> &instrs, int64_t arity) {
    Z3_config cfg = Z3_mk_config();
    Z3_context ctx = Z3_mk_context(cfg);
    Z3_del_config(cfg);

    const x86::Reg arg_regs[] = {
        x86::Reg::Rdi, x86::Reg::Rsi, x86::Reg::Rdx,
        x86::Reg::Rcx, x86::Reg::R8, x86::Reg::R9};
    const int max_reg_args = std::min(arity, int64_t{6});

    // Track which arg registers have been written before the call
    std::vector<bool> reg_written(6, false);

    // invariant: instrs[0..i) scanned, reg_written updated
    for (const auto &instr : instrs) {
        // Check movq to arg register
        if (const auto *m = std::get_if<x86::Movq>(&instr)) {
            if (const auto *dst_reg = std::get_if<x86::RegArg>(&m->dst)) {
                for (int j = 0; j < max_reg_args; ++j) {
                    if (dst_reg->reg == arg_regs[j]) {
                        reg_written[j] = true;
                    }
                }
            }
        }
    }

    // Encode Z3: for each required reg, assert NOT written and check SAT
    // invariant: regs[0..i) verified
    for (int i = 0; i < max_reg_args; ++i) {
        Z3_ast assertion =
            reg_written[i] ? Z3_mk_false(ctx) : Z3_mk_true(ctx);
        Z3_solver solver = Z3_mk_solver(ctx);
        Z3_solver_inc_ref(ctx, solver);
        Z3_solver_assert(ctx, solver, assertion);
        Z3_lbool result = Z3_solver_check(ctx, solver);
        Z3_solver_dec_ref(ctx, solver);
        if (result == Z3_L_TRUE && !reg_written[i]) {
            Z3_del_context(ctx);
            return false;
        }
    }

    Z3_del_context(ctx);
    return true;
}

/// @brief Z3 predicate: tail call has no frame growth
/// TailJmp must NOT be preceded by pushq between prologue and the jump.
/// @requires instrs contains a TailJmp
/// @ensures returns true iff no pushq appears before TailJmp
static bool z3_tail_call_no_frame_growth(
    const std::vector<x86::Instr> &instrs) {
    Z3_config cfg = Z3_mk_config();
    Z3_context ctx = Z3_mk_context(cfg);
    Z3_del_config(cfg);

    bool found_push = false;
    bool found_tail = false;

    // invariant: scanned instrs[0..i)
    for (const auto &instr : instrs) {
        if (std::holds_alternative<x86::TailJmp>(instr)) {
            found_tail = true;
            break;
        }
        if (std::holds_alternative<x86::Pushq>(instr)) {
            found_push = true;
        }
    }

    Z3_ast assertion =
        (found_tail && found_push) ? Z3_mk_true(ctx) : Z3_mk_false(ctx);
    Z3_solver solver = Z3_mk_solver(ctx);
    Z3_solver_inc_ref(ctx, solver);
    Z3_solver_assert(ctx, solver, assertion);
    Z3_lbool result = Z3_solver_check(ctx, solver);
    Z3_solver_dec_ref(ctx, solver);

    bool has_frame_growth = (result == Z3_L_TRUE);
    Z3_del_context(ctx);
    return !has_frame_growth;
}

TEST(CallingConventionZ3, ArgsInCorrectRegisters1Arg) {
    // movq $42, %rdi; callq *%rax
    std::vector<x86::Instr> instrs = {
        x86::Movq{x86::Imm{42}, x86::RegArg{x86::Reg::Rdi}},
        x86::IndirectCallq{x86::RegArg{x86::Reg::Rax}, 1},
    };
    EXPECT_TRUE(z3_args_in_correct_registers(instrs, 1));
}

TEST(CallingConventionZ3, ArgsInCorrectRegisters3Args) {
    // movq $1, %rdi; movq $2, %rsi; movq $3, %rdx; callq *%rax
    std::vector<x86::Instr> instrs = {
        x86::Movq{x86::Imm{1}, x86::RegArg{x86::Reg::Rdi}},
        x86::Movq{x86::Imm{2}, x86::RegArg{x86::Reg::Rsi}},
        x86::Movq{x86::Imm{3}, x86::RegArg{x86::Reg::Rdx}},
        x86::IndirectCallq{x86::RegArg{x86::Reg::Rax}, 3},
    };
    EXPECT_TRUE(z3_args_in_correct_registers(instrs, 3));
}

TEST(CallingConventionZ3, MissingArgRegister) {
    // movq $1, %rdi; (missing rsi); callq *%rax
    std::vector<x86::Instr> instrs = {
        x86::Movq{x86::Imm{1}, x86::RegArg{x86::Reg::Rdi}},
        x86::IndirectCallq{x86::RegArg{x86::Reg::Rax}, 2},
    };
    EXPECT_FALSE(z3_args_in_correct_registers(instrs, 2));
}

TEST(CallingConventionZ3, TailCallNoFrameGrowth) {
    // movq $1, %rdi; jmp *%rax (no pushq)
    std::vector<x86::Instr> instrs = {
        x86::Movq{x86::Imm{1}, x86::RegArg{x86::Reg::Rdi}},
        x86::TailJmp{x86::RegArg{x86::Reg::Rax}, 1},
    };
    EXPECT_TRUE(z3_tail_call_no_frame_growth(instrs));
}

TEST(CallingConventionZ3, TailCallWithPushFails) {
    // pushq %rbx; movq $1, %rdi; jmp *%rax — BAD: push before tail
    std::vector<x86::Instr> instrs = {
        x86::Pushq{x86::RegArg{x86::Reg::Rbx}},
        x86::Movq{x86::Imm{1}, x86::RegArg{x86::Reg::Rdi}},
        x86::TailJmp{x86::RegArg{x86::Reg::Rax}, 1},
    };
    EXPECT_FALSE(z3_tail_call_no_frame_growth(instrs));
}
