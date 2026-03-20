#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "ast.h"
#include "passes/rco.h"
#include "passes/uniquify.h"
#include "passes/shrink.h"
#include "passes/uncover_get.h"

using namespace mc;

/// Check whether an expression is atomic (IntExpr or VarExpr).
static bool is_atomic(const Expr *e) {
  return e->kind() == NodeKind::Int || e->kind() == NodeKind::Var;
}

/// Iteratively verify all operands of Unary/Binary are atomic.
/// @requires root != nullptr
/// @ensures returns true iff every Unary/Binary operand is Int or Var
static bool check_all_atomic(const Expr *root) {
  std::vector<const Expr *> stack;
  stack.push_back(root);

  // decreases stack.size()
  // invariant all visited Unary/Binary nodes had atomic operands
  while (!stack.empty()) {
    const auto *e = stack.back();
    stack.pop_back();

    switch (e->kind()) {
    case NodeKind::Unary: {
      const auto *ue = expr_cast<UnaryExpr>(e);
      if (!is_atomic(ue->operand.get())) {
        return false;
      }
      break;
    }
    case NodeKind::Binary: {
      const auto *be = expr_cast<BinaryExpr>(e);
      if (!is_atomic(be->lhs.get()) || !is_atomic(be->rhs.get())) {
        return false;
      }
      break;
    }
    case NodeKind::Let: {
      const auto *le = expr_cast<LetExpr>(e);
      stack.push_back(le->body.get());
      stack.push_back(le->init.get());
      break;
    }
    default:
      break;
    }
    // Atoms and ReadExpr are fine
  }
  return true;
}

/// Helper: uniquify then RCO.
static std::unique_ptr<Program> run_rco(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto s0__ = shrink(prog); auto u = uniquify(*s0__);
  auto ug = uncover_get(*u);
  return remove_complex_operands(*ug);
}

TEST(RCO, AlreadyAtomic) {
  // 10 + 32 — operands already atomic
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  auto result = run_rco(std::move(e));
  EXPECT_TRUE(check_all_atomic(result->body.get()));
}

TEST(RCO, NestedOperand) {
  // 10 + -(12 + 20) — inner (12+20) is complex operand of neg
  auto inner = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(12),
      std::make_unique<IntExpr>(20));
  auto neg = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::move(inner));
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::move(neg));
  auto result = run_rco(std::move(e));
  EXPECT_TRUE(check_all_atomic(result->body.get()));
}

TEST(RCO, DeepNest) {
  // let a = 5; a + (a + a)
  auto sum = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<VarExpr>("a"),
      std::make_unique<VarExpr>("a"));
  auto outer = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<VarExpr>("a"),
      std::move(sum));
  auto e = std::make_unique<LetExpr>(
      "a", std::make_unique<IntExpr>(5), std::move(outer));
  auto result = run_rco(std::move(e));
  EXPECT_TRUE(check_all_atomic(result->body.get()));
}

// ---- Phase 4: while/begin/set!/void ----

TEST(RCO, WhilePassesThrough) {
  // while (true) { 42 }
  auto e = std::make_unique<WhileExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(42));
  auto result = run_rco(std::move(e));
  ASSERT_EQ(result->body->kind(), NodeKind::While);
}

TEST(RCO, SetBangPassesThrough) {
  // let x = 0; set! x 42
  auto e = std::make_unique<LetExpr>(
      "x", std::make_unique<IntExpr>(0),
      std::make_unique<SetBangExpr>(
          "x", std::make_unique<IntExpr>(42)));
  auto result = run_rco(std::move(e));
  // Body should be let with set! inside
  ASSERT_EQ(result->body->kind(), NodeKind::Let);
}

TEST(RCO, BeginPassesThrough) {
  std::vector<std::unique_ptr<Expr>> bexprs;
  bexprs.push_back(std::make_unique<IntExpr>(1));
  bexprs.push_back(std::make_unique<IntExpr>(2));
  auto e = std::make_unique<BeginExpr>(std::move(bexprs));
  auto result = run_rco(std::move(e));
  ASSERT_EQ(result->body->kind(), NodeKind::Begin);
}

TEST(RCO, VoidPassesThrough) {
  auto result = run_rco(std::make_unique<VoidExpr>());
  ASSERT_EQ(result->body->kind(), NodeKind::Void);
}
