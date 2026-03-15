#include <gtest/gtest.h>

#include <memory>
#include <variant>

#include "ast.h"
#include "ir/c_ir.h"
#include "ir/x86_ir.h"
#include "passes/explicate_control.h"
#include "passes/rco.h"
#include "passes/select_instructions.h"
#include "passes/uniquify.h"
#include "passes/shrink.h"

/// Helper: run pipeline through select_instructions.
static x86::X86Program run_select(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto s0__ = shrink(prog); auto u = uniquify(*s0__);
  auto r = remove_complex_operands(*u);
  auto c = explicate_control(*r);
  return select_instructions(c);
}

/// Check if any instruction in the block matches a type.
template <typename T>
static bool has_instr(const x86::Block &blk) {
  // invariant checked[0..i) did not match T
  for (const auto &instr : blk.instrs) {
    if (std::holds_alternative<T>(instr)) {
      return true;
    }
  }
  return false;
}

TEST(SelectInstructions, IntLiteralHasMovq) {
  auto prog = run_select(std::make_unique<IntExpr>(42));
  ASSERT_NE(prog.blocks.find("start"), prog.blocks.end());
  EXPECT_TRUE(has_instr<x86::Movq>(prog.blocks.at("start")));
}

TEST(SelectInstructions, AddHasAddq) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  auto prog = run_select(std::move(e));
  ASSERT_NE(prog.blocks.find("start"), prog.blocks.end());
  EXPECT_TRUE(has_instr<x86::Addq>(prog.blocks.at("start")));
}

TEST(SelectInstructions, NegHasNegq) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<IntExpr>(10));
  auto prog = run_select(std::move(e));
  ASSERT_NE(prog.blocks.find("start"), prog.blocks.end());
  EXPECT_TRUE(has_instr<x86::Negq>(prog.blocks.at("start")));
}

TEST(SelectInstructions, HasJmpToConclusion) {
  auto prog = run_select(std::make_unique<IntExpr>(42));
  ASSERT_NE(prog.blocks.find("start"), prog.blocks.end());
  const auto &blk = prog.blocks.at("start");
  EXPECT_TRUE(has_instr<x86::Jmp>(blk));
}

TEST(SelectInstructions, ReadHasCallq) {
  auto prog = run_select(std::make_unique<ReadExpr>());
  ASSERT_NE(prog.blocks.find("start"), prog.blocks.end());
  EXPECT_TRUE(has_instr<x86::Callq>(prog.blocks.at("start")));
}

// ---- Phase 3: comparison / not / if tests ----

/// Check if any block in program contains instruction type T.
template <typename T>
static bool any_block_has(const x86::X86Program &prog) {
  // invariant: checked blocks so far had no T
  for (const auto &[label, blk] : prog.blocks) {
    if (has_instr<T>(blk)) {
      return true;
    }
  }
  return false;
}

TEST(SelectInstructions, ComparisonHasCmpq) {
  // if (1 < 2) { 42 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Lt,
          std::make_unique<IntExpr>(1),
          std::make_unique<IntExpr>(2)),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto prog = run_select(std::move(e));
  EXPECT_TRUE(any_block_has<x86::Cmpq>(prog));
}

TEST(SelectInstructions, ComparisonHasJmpIf) {
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Lt,
          std::make_unique<IntExpr>(1),
          std::make_unique<IntExpr>(2)),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto prog = run_select(std::move(e));
  EXPECT_TRUE(any_block_has<x86::JmpIf>(prog));
}

TEST(SelectInstructions, NotHasXorq) {
  // let x = not true; x  — not in value position uses xorq
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<UnaryExpr>(
          UnaryOp::Not, std::make_unique<BoolExpr>(true)),
      std::make_unique<VarExpr>("x"));
  auto prog = run_select(std::move(e));
  EXPECT_TRUE(any_block_has<x86::Xorq>(prog));
}

TEST(SelectInstructions, IfHasMultipleBlocks) {
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(0));
  auto prog = run_select(std::move(e));
  EXPECT_GT(prog.blocks.size(), 1U);
}

TEST(SelectInstructions, EqHasCmpqSetCC) {
  // let x = 1 == 1; if (x) { 42 } else { 0 }
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<BinaryExpr>(
          BinaryOp::Eq,
          std::make_unique<IntExpr>(1),
          std::make_unique<IntExpr>(1)),
      std::make_unique<IfExpr>(
          std::make_unique<VarExpr>("x"),
          std::make_unique<IntExpr>(42),
          std::make_unique<IntExpr>(0)));
  auto prog = run_select(std::move(e));
  EXPECT_TRUE(any_block_has<x86::Cmpq>(prog));
  EXPECT_TRUE(any_block_has<x86::SetCC>(prog));
}
