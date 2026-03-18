#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ast.h"
#include "passes/uniquify.h"

/// Iteratively collect all LetExpr variable names in the AST.
/// @requires root != nullptr
/// @ensures result contains every let-bound name in tree order
static std::vector<std::string> collect_let_vars(const Expr *root) {
  std::vector<std::string> names;
  std::vector<const Expr *> stack;
  stack.push_back(root);

  // decreases stack.size()
  // invariant names holds let-vars from already-visited nodes
  while (!stack.empty()) {
    const auto *e = stack.back();
    stack.pop_back();

    switch (e->kind()) {
    case NodeKind::Let: {
      const auto *le = static_cast<const LetExpr *>(e);
      names.push_back(le->var);
      stack.push_back(le->body.get());
      stack.push_back(le->init.get());
      break;
    }
    case NodeKind::Unary: {
      const auto *ue = static_cast<const UnaryExpr *>(e);
      stack.push_back(ue->operand.get());
      break;
    }
    case NodeKind::Binary: {
      const auto *be = static_cast<const BinaryExpr *>(e);
      stack.push_back(be->rhs.get());
      stack.push_back(be->lhs.get());
      break;
    }
    default:
      break;
    }
    // IntExpr, VarExpr, ReadExpr are leaves
  }
  return names;
}

/// Check all let-bound names are unique.
static bool all_unique(const std::vector<std::string> &names) {
  std::set<std::string> seen;
  // invariant seen contains names[0..i)
  for (const auto &n : names) {
    if (!seen.insert(n).second) {
      return false;
    }
  }
  return true;
}

TEST(Uniquify, NoShadow) {
  // let x = 5; let y = 37; x + y  — already unique, should stay valid
  auto inner = std::make_unique<LetExpr>(
      "y",
      std::make_unique<IntExpr>(37),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("x"),
          std::make_unique<VarExpr>("y")));
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(5),
      std::move(inner));
  Program prog(std::move(e));

  auto result = uniquify(prog);
  auto vars = collect_let_vars(result->body.get());
  EXPECT_TRUE(all_unique(vars));
}

TEST(Uniquify, Shadow) {
  // let x = 10; let x = 32; x + 10
  auto inner = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("x"),
          std::make_unique<IntExpr>(10)));
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(10),
      std::move(inner));
  Program prog(std::move(e));

  auto result = uniquify(prog);
  auto vars = collect_let_vars(result->body.get());
  EXPECT_EQ(vars.size(), 2U);
  EXPECT_TRUE(all_unique(vars));
}

TEST(Uniquify, TripleShadow) {
  // let x = 1; let x = 2; let x = 3; x
  auto e3 = std::make_unique<LetExpr>(
      "x", std::make_unique<IntExpr>(3),
      std::make_unique<VarExpr>("x"));
  auto e2 = std::make_unique<LetExpr>(
      "x", std::make_unique<IntExpr>(2), std::move(e3));
  auto e1 = std::make_unique<LetExpr>(
      "x", std::make_unique<IntExpr>(1), std::move(e2));
  Program prog(std::move(e1));

  auto result = uniquify(prog);
  auto vars = collect_let_vars(result->body.get());
  EXPECT_EQ(vars.size(), 3U);
  EXPECT_TRUE(all_unique(vars));
}
