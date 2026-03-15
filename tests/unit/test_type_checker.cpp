#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "type_checker.h"

/// Helper: type-check a single expression.
static Type check(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  return type_check(prog);
}

// ---- Well-typed accepts ----

TEST(TypeChecker, IntArith) {
  // 10 + 32
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  EXPECT_EQ(check(std::move(e)), Type::Int);
}

TEST(TypeChecker, IntSub) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Sub,
      std::make_unique<IntExpr>(52),
      std::make_unique<IntExpr>(10));
  EXPECT_EQ(check(std::move(e)), Type::Int);
}

TEST(TypeChecker, BoolLiteral) {
  EXPECT_EQ(check(std::make_unique<BoolExpr>(true)), Type::Bool);
}

TEST(TypeChecker, NotBool) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Not, std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, BoolLogicAnd) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::And,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, BoolLogicOr) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Or,
      std::make_unique<BoolExpr>(false),
      std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, IfExpr) {
  // if (true) { 42 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  EXPECT_EQ(check(std::move(e)), Type::Int);
}

TEST(TypeChecker, IfBoolBranches) {
  // if (true) { false } else { true }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false),
      std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, ComparisonLt) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Lt,
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(2));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, ComparisonGe) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Ge,
      std::make_unique<IntExpr>(5),
      std::make_unique<IntExpr>(3));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, EqInt) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Eq,
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(1));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, EqBool) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Eq,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), Type::Bool);
}

TEST(TypeChecker, LetWithIf) {
  // let x = 10; if (x < 20) { x } else { 0 }
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(10),
      std::make_unique<IfExpr>(
          std::make_unique<BinaryExpr>(
              BinaryOp::Lt,
              std::make_unique<VarExpr>("x"),
              std::make_unique<IntExpr>(20)),
          std::make_unique<VarExpr>("x"),
          std::make_unique<IntExpr>(0)));
  EXPECT_EQ(check(std::move(e)), Type::Int);
}

TEST(TypeChecker, NegInt) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<IntExpr>(10));
  EXPECT_EQ(check(std::move(e)), Type::Int);
}

// ---- Ill-typed rejects ----

TEST(TypeChecker, NotIntThrows) {
  // not 42
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Not, std::make_unique<IntExpr>(42));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, AddBoolThrows) {
  // 1 + true
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(1),
      std::make_unique<BoolExpr>(true));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, IfCondNotBoolThrows) {
  // if (42) { 1 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(0));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, IfBranchMismatchThrows) {
  // if (true) { 42 } else { false }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(42),
      std::make_unique<BoolExpr>(false));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, CmpBoolThrows) {
  // 1 < true
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Lt,
      std::make_unique<IntExpr>(1),
      std::make_unique<BoolExpr>(true));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, AddBoolsThrows) {
  // true + false
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, NegBoolThrows) {
  // -(true)
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<BoolExpr>(true));
  EXPECT_THROW(check(std::move(e)), TypeError);
}
