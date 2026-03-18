#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "ir/c_ir.h"
#include "passes/explicate_control.h"
#include "passes/rco.h"
#include "passes/shrink.h"
#include "passes/uncover_get.h"
#include "passes/uniquify.h"

/// Helper: run full pipeline up to explicate_control.
static cir::CProgram run_explicate(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto s = shrink(prog);
  auto u = uniquify(*s);
  auto ug = uncover_get(*u);
  auto r = remove_complex_operands(*ug);
  return explicate_control(*r);
}

TEST(ExplicateControl, HasStartBlock) {
  auto cprog = run_explicate(std::make_unique<IntExpr>(42));
  EXPECT_NE(cprog.blocks.find("start"), cprog.blocks.end());
}

TEST(ExplicateControl, IntLiteralReturn) {
  auto cprog = run_explicate(std::make_unique<IntExpr>(42));
  const auto &blk = cprog.blocks.at("start");
  // Tail should be Return with AtomExpr(int 42)
  ASSERT_TRUE(std::holds_alternative<cir::Return>(blk.tail));
  const auto &ret = std::get<cir::Return>(blk.tail);
  EXPECT_TRUE(std::holds_alternative<cir::AtomExpr>(ret.expr));
  if (std::holds_alternative<cir::AtomExpr>(ret.expr)) {
    const auto &ae = std::get<cir::AtomExpr>(ret.expr);
    EXPECT_TRUE(std::holds_alternative<cir::IntAtom>(ae.atom));
  }
}

TEST(ExplicateControl, AddHasStatements) {
  // 10 + 32 — may produce assigns depending on RCO
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  auto cprog = run_explicate(std::move(e));
  EXPECT_NE(cprog.blocks.find("start"), cprog.blocks.end());
}

TEST(ExplicateControl, LetProducesAssign) {
  // let x = 32; x
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<VarExpr>("x"));
  auto cprog = run_explicate(std::move(e));
  const auto &blk = cprog.blocks.at("start");
  EXPECT_GE(blk.stmts.size(), 1U);
}

// ---- Phase 3: if / comparison tests ----

/// Check if any block has an IfStmt tail.
static bool has_if_tail(const cir::CProgram &cprog) {
  // invariant: checked blocks so far had no IfStmt
  for (const auto &[label, blk] : cprog.blocks) {
    if (std::holds_alternative<cir::IfStmt>(blk.tail)) {
      return true;
    }
  }
  return false;
}

/// Check if any block has a Goto tail.
static bool has_goto_tail(const cir::CProgram &cprog) {
  // invariant: checked blocks so far had no Goto
  for (const auto &[label, blk] : cprog.blocks) {
    if (std::holds_alternative<cir::Goto>(blk.tail)) {
      return true;
    }
  }
  return false;
}

TEST(ExplicateControl, IfProducesIfStmt) {
  // if (1 < 2) { 42 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Lt,
          std::make_unique<IntExpr>(1),
          std::make_unique<IntExpr>(2)),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto cprog = run_explicate(std::move(e));
  EXPECT_TRUE(has_if_tail(cprog));
  // Then/else blocks should have Goto or Return
  EXPECT_GT(cprog.blocks.size(), 1U);
}

TEST(ExplicateControl, ComparisonProducesIfStmt) {
  // if (true) { 1 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(0));
  auto cprog = run_explicate(std::move(e));
  // Bool condition still generates blocks
  EXPECT_GT(cprog.blocks.size(), 1U);
}

TEST(ExplicateControl, NestedIfMultipleBlocks) {
  // if (1 < 2) { if (3 < 4) { 42 } else { 0 } } else { 1 }
  auto inner = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Lt,
          std::make_unique<IntExpr>(3),
          std::make_unique<IntExpr>(4)),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Lt,
          std::make_unique<IntExpr>(1),
          std::make_unique<IntExpr>(2)),
      std::move(inner),
      std::make_unique<IntExpr>(1));
  auto cprog = run_explicate(std::move(e));
  EXPECT_TRUE(has_if_tail(cprog));
  // Nested if produces many blocks
  EXPECT_GE(cprog.blocks.size(), 4U);
}

// ---- Phase 4: while/set!/begin ----

TEST(ExplicateControl, WhileProducesLoop) {
    // let x = 0; while (x < 3) (set! x (+ x 1))
    auto e = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0),
        std::make_unique<WhileExpr>(
            std::make_unique<BinaryExpr>(
                BinaryOp::Lt,
                std::make_unique<VarExpr>("x"),
                std::make_unique<IntExpr>(3)),
            std::make_unique<SetBangExpr>(
                "x", std::make_unique<BinaryExpr>(
                    BinaryOp::Add,
                    std::make_unique<VarExpr>("x"),
                    std::make_unique<IntExpr>(1)))));
    auto cprog = run_explicate(std::move(e));
    // Should have loop_entry, loop_body, loop_exit blocks + start
    EXPECT_GE(cprog.blocks.size(), 4U);
    // Should have Goto back-edges (loop)
    EXPECT_TRUE(has_goto_tail(cprog));
}

TEST(ExplicateControl, SetBangProducesAssign) {
    // let x = 0; set! x 42
    auto e = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0),
        std::make_unique<SetBangExpr>(
            "x", std::make_unique<IntExpr>(42)));
    auto cprog = run_explicate(std::move(e));
    EXPECT_NE(cprog.blocks.find("start"), cprog.blocks.end());
    // start block should have assignment stmts
    EXPECT_GE(cprog.blocks.at("start").stmts.size(), 1U);
}

TEST(ExplicateControl, BeginChainsEffects) {
    // begin { 1; 2; 42 }
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<IntExpr>(1));
    bexprs.push_back(std::make_unique<IntExpr>(2));
    bexprs.push_back(std::make_unique<IntExpr>(42));
    auto e = std::make_unique<BeginExpr>(std::move(bexprs));
    auto cprog = run_explicate(std::move(e));
    EXPECT_NE(cprog.blocks.find("start"), cprog.blocks.end());
}
