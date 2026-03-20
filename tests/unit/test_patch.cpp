#include <gtest/gtest.h>

#include <memory>
#include <variant>

#include "ast.h"
#include "ir/x86_ir.h"
#include "passes/assign_homes.h"
#include "passes/explicate_control.h"
#include "passes/patch_instructions.h"
#include "passes/rco.h"
#include "passes/select_instructions.h"
#include "passes/uniquify.h"
#include "passes/shrink.h"

using namespace mc;

/// Helper: run pipeline through patch_instructions.
static x86::X86Program run_patch(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto s0__ = shrink(prog); auto u = uniquify(*s0__);
  auto r = remove_complex_operands(*u);
  auto c = explicate_control(*r);
  auto s = select_instructions(c);
  auto a = assign_homes(s);
  return patch_instructions(a);
}

/// Check no instruction has two memory (Deref) operands.
static bool no_double_deref(const x86::X86Program &prog) {
  auto is_deref = [](const x86::Arg &a) {
    return std::holds_alternative<x86::Deref>(a);
  };

  // invariant all checked blocks pass constraint
  for (const auto &[label, blk] : prog.blocks) {
    // invariant all checked instrs pass constraint
    for (const auto &instr : blk.instrs) {
      if (std::holds_alternative<x86::Movq>(instr)) {
        const auto &m = std::get<x86::Movq>(instr);
        if (is_deref(m.src) && is_deref(m.dst)) return false;
      } else if (std::holds_alternative<x86::Addq>(instr)) {
        const auto &a = std::get<x86::Addq>(instr);
        if (is_deref(a.src) && is_deref(a.dst)) return false;
      } else if (std::holds_alternative<x86::Subq>(instr)) {
        const auto &s = std::get<x86::Subq>(instr);
        if (is_deref(s.src) && is_deref(s.dst)) return false;
      }
    }
  }
  return true;
}

TEST(PatchInstructions, IntLiteral) {
  auto prog = run_patch(std::make_unique<IntExpr>(42));
  EXPECT_TRUE(no_double_deref(prog));
}

TEST(PatchInstructions, LetBinding) {
  // let x = 32; let y = x; y
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<LetExpr>(
          "y",
          std::make_unique<VarExpr>("x"),
          std::make_unique<VarExpr>("y")));
  auto prog = run_patch(std::move(e));
  EXPECT_TRUE(no_double_deref(prog));
}

TEST(PatchInstructions, AddWithLet) {
  // let x = 5; let y = 10; x + y
  auto inner = std::make_unique<LetExpr>(
      "y",
      std::make_unique<IntExpr>(10),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("x"),
          std::make_unique<VarExpr>("y")));
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(5),
      std::move(inner));
  auto prog = run_patch(std::move(e));
  EXPECT_TRUE(no_double_deref(prog));
}
