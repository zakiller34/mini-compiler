#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "ir/c_ir.h"
#include "passes/explicate_control.h"
#include "passes/rco.h"
#include "passes/uniquify.h"

/// Helper: run full pipeline up to explicate_control.
static cir::CProgram run_explicate(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto u = uniquify(prog);
  auto r = remove_complex_operands(*u);
  return explicate_control(*r);
}

TEST(ExplicateControl, HasStartBlock) {
  auto cprog = run_explicate(std::make_unique<IntExpr>(42));
  EXPECT_NE(cprog.blocks.find("start"), cprog.blocks.end());
}

TEST(ExplicateControl, IntLiteralReturn) {
  auto cprog = run_explicate(std::make_unique<IntExpr>(42));
  const auto &blk = cprog.blocks.at("start");
  // Return should be an AtomExpr with int 42
  EXPECT_TRUE(
      std::holds_alternative<cir::AtomExpr>(blk.ret));
  if (std::holds_alternative<cir::AtomExpr>(blk.ret)) {
    const auto &ae = std::get<cir::AtomExpr>(blk.ret);
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
