#include <gtest/gtest.h>

#include <memory>
#include <variant>

#include "ast.h"
#include "ir/c_ir.h"
#include "ir/x86_ir.h"
#include "passes/explicate_control.h"
#include "passes/rco.h"
#include "passes/select_instructions.h"
#include "passes/expose_allocation.h"
#include "passes/limit_functions.h"
#include "passes/reveal_functions.h"
#include "passes/uniquify.h"
#include "passes/shrink.h"
#include "passes/uncover_get.h"

using namespace mc;

/// Helper: run pipeline through select_instructions.
static x86::X86Program run_select(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto s0__ = shrink(prog); auto u = uniquify(*s0__);
  auto ug = uncover_get(*u);
  auto r = remove_complex_operands(*ug);
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

// ---- Phase 6: function instruction selection ----

/// Helper: run pipeline through select_instructions with defs.
static x86::X86Program run_select_with_defs(
    std::unique_ptr<Expr> body, std::vector<DefNode> defs) {
    Program prog(std::move(defs), std::move(body));
    auto s0__ = shrink(prog); auto u = uniquify(*s0__);
    auto rf = reveal_functions(*u);
    auto lf = limit_functions(*rf);
    const auto &lf_ref = lf ? *lf : *rf;
    auto ug = uncover_get(lf_ref);
    auto ea = expose_allocation(*ug);
    auto r = remove_complex_operands(*ea);
    auto c = explicate_control(*r);
    return select_instructions(c);
}

TEST(SelectInstructions, FunRefHasLeaq) {
    // fn foo(x: int): int { x }; foo
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(42));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("foo"), std::move(args));

    auto prog = run_select_with_defs(std::move(body), std::move(defs));
    // Should have leaq for function reference
    EXPECT_TRUE(any_block_has<x86::Leaq>(prog));
}

TEST(SelectInstructions, CallHasIndirectCallq) {
    // fn foo(x: int): int { x }; foo(42)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(42));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("foo"), std::move(args));

    auto prog = run_select_with_defs(std::move(body), std::move(defs));
    // Check any block or def block has IndirectCallq
    bool found = any_block_has<x86::IndirectCallq>(prog);
    // invariant: checked defs[0..i) did not have IndirectCallq
    for (const auto &def : prog.defs) {
        for (const auto &[label, blk] : def.blocks) {
            if (has_instr<x86::IndirectCallq>(blk)) found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(SelectInstructions, FnDefHasBlocks) {
    // fn foo(x: int): int { x }; foo(42)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(42));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("foo"), std::move(args));

    auto prog = run_select_with_defs(std::move(body), std::move(defs));
    EXPECT_EQ(prog.defs.size(), 1u);
    EXPECT_FALSE(prog.defs[0].blocks.empty());
}
