#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "ast.h"
#include "passes/rco.h"
#include "passes/uniquify.h"

/// Check whether an expression is atomic (IntExpr or VarExpr).
static bool is_atomic(const Expr *e) {
  return dynamic_cast<const IntExpr *>(e) != nullptr ||
         dynamic_cast<const VarExpr *>(e) != nullptr;
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

    if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
      if (!is_atomic(ue->operand.get())) {
        return false;
      }
    } else if (const auto *be = dynamic_cast<const BinaryExpr *>(e)) {
      if (!is_atomic(be->lhs.get()) || !is_atomic(be->rhs.get())) {
        return false;
      }
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
      stack.push_back(le->body.get());
      stack.push_back(le->init.get());
    }
    // Atoms and ReadExpr are fine
  }
  return true;
}

/// Helper: uniquify then RCO.
static std::unique_ptr<Program> run_rco(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto u = uniquify(prog);
  return remove_complex_operands(*u);
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
