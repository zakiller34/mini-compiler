#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "ast.h"
#include "passes/shrink.h"

/// Check that no And/Or BinaryOp remains in an expression tree.
/// Uses iterative stack traversal.
static bool no_and_or(const Expr &expr) {
  std::vector<const Expr *> stack;
  stack.push_back(&expr);
  // invariant: all popped exprs so far have no And/Or
  while (!stack.empty()) {
    const auto *e = stack.back();
    stack.pop_back();
    switch (e->kind()) {
    case NodeKind::Binary: {
      const auto *bin = static_cast<const BinaryExpr *>(e);
      if (bin->op == BinaryOp::And || bin->op == BinaryOp::Or) {
        return false;
      }
      stack.push_back(bin->lhs.get());
      stack.push_back(bin->rhs.get());
      break;
    }
    case NodeKind::Unary: {
      const auto *un = static_cast<const UnaryExpr *>(e);
      stack.push_back(un->operand.get());
      break;
    }
    case NodeKind::If: {
      const auto *ife = static_cast<const IfExpr *>(e);
      stack.push_back(ife->cond.get());
      stack.push_back(ife->then_branch.get());
      stack.push_back(ife->else_branch.get());
      break;
    }
    case NodeKind::Let: {
      const auto *le = static_cast<const LetExpr *>(e);
      stack.push_back(le->init.get());
      stack.push_back(le->body.get());
      break;
    }
    default:
      break;
    }
  }
  return true;
}

/// Count IfExpr nodes in tree.
static int count_if(const Expr &expr) {
  int count = 0;
  std::vector<const Expr *> stack;
  stack.push_back(&expr);
  // invariant: count = # IfExpr in already-visited nodes
  while (!stack.empty()) {
    const auto *e = stack.back();
    stack.pop_back();
    switch (e->kind()) {
    case NodeKind::If: {
      ++count;
      const auto *ife = static_cast<const IfExpr *>(e);
      stack.push_back(ife->cond.get());
      stack.push_back(ife->then_branch.get());
      stack.push_back(ife->else_branch.get());
      break;
    }
    case NodeKind::Binary: {
      const auto *bin = static_cast<const BinaryExpr *>(e);
      stack.push_back(bin->lhs.get());
      stack.push_back(bin->rhs.get());
      break;
    }
    case NodeKind::Unary: {
      const auto *un = static_cast<const UnaryExpr *>(e);
      stack.push_back(un->operand.get());
      break;
    }
    case NodeKind::Let: {
      const auto *le = static_cast<const LetExpr *>(e);
      stack.push_back(le->init.get());
      stack.push_back(le->body.get());
      break;
    }
    default:
      break;
    }
  }
  return count;
}

TEST(Shrink, AndBecomesIf) {
  // and(a, b) -> if(a, b, false)
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::And,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false));
  Program prog(std::move(e));
  auto result = shrink(prog);
  EXPECT_TRUE(no_and_or(*result->body));
  // Should produce an IfExpr
  EXPECT_GE(count_if(*result->body), 1);
}

TEST(Shrink, OrBecomesIf) {
  // or(a, b) -> if(a, true, b)
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Or,
      std::make_unique<BoolExpr>(false),
      std::make_unique<BoolExpr>(true));
  Program prog(std::move(e));
  auto result = shrink(prog);
  EXPECT_TRUE(no_and_or(*result->body));
  EXPECT_GE(count_if(*result->body), 1);
}

TEST(Shrink, NestedAndOr) {
  // and(or(a, b), c) -> nested if, no and/or
  auto inner = std::make_unique<BinaryExpr>(
      BinaryOp::Or,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false));
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::And,
      std::move(inner),
      std::make_unique<BoolExpr>(true));
  Program prog(std::move(e));
  auto result = shrink(prog);
  EXPECT_TRUE(no_and_or(*result->body));
  // At least 2 IfExprs from and + or
  EXPECT_GE(count_if(*result->body), 2);
}

TEST(Shrink, NonLogicPassesThrough) {
  // 10 + 32 unchanged
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  Program prog(std::move(e));
  auto result = shrink(prog);
  EXPECT_TRUE(no_and_or(*result->body));
  EXPECT_EQ(count_if(*result->body), 0);
  // Still a BinaryExpr with Add
  ASSERT_EQ(result->body->kind(), NodeKind::Binary);
  const auto *bin = static_cast<const BinaryExpr *>(result->body.get());
  EXPECT_EQ(bin->op, BinaryOp::Add);
}

TEST(Shrink, IfPassesThrough) {
  // if (true) { 1 } else { 0 } — already desugared
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(0));
  Program prog(std::move(e));
  auto result = shrink(prog);
  EXPECT_EQ(count_if(*result->body), 1);
}

TEST(Shrink, IntLiteralUnchanged) {
  Program prog(std::make_unique<IntExpr>(42));
  auto result = shrink(prog);
  ASSERT_EQ(result->body->kind(), NodeKind::Int);
  const auto *i = static_cast<const IntExpr *>(result->body.get());
  EXPECT_EQ(i->value, 42);
}

// ---- Phase 4: while/begin/set!/void ----

TEST(Shrink, WhilePassesThrough) {
  auto e = std::make_unique<WhileExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(1));
  Program prog(std::move(e));
  auto result = shrink(prog);
  ASSERT_EQ(result->body->kind(), NodeKind::While);
}

TEST(Shrink, SetBangPassesThrough) {
  auto e = std::make_unique<SetBangExpr>(
      "x", std::make_unique<IntExpr>(42));
  Program prog(std::move(e));
  auto result = shrink(prog);
  ASSERT_EQ(result->body->kind(), NodeKind::SetBang);
}

TEST(Shrink, BeginPassesThrough) {
  std::vector<std::unique_ptr<Expr>> bexprs;
  bexprs.push_back(std::make_unique<IntExpr>(1));
  bexprs.push_back(std::make_unique<IntExpr>(2));
  auto e = std::make_unique<BeginExpr>(std::move(bexprs));
  Program prog(std::move(e));
  auto result = shrink(prog);
  ASSERT_EQ(result->body->kind(), NodeKind::Begin);
}

TEST(Shrink, VoidPassesThrough) {
  Program prog(std::make_unique<VoidExpr>());
  auto result = shrink(prog);
  ASSERT_EQ(result->body->kind(), NodeKind::Void);
}
