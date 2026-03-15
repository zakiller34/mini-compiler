#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include "ast.h"
#include "interpreter.h"

/// Helper: interpret program, extract int64_t result.
static int64_t run(std::unique_ptr<Expr> body,
                   const std::string &input = "") {
  std::istringstream in(input);
  Program prog(std::move(body));
  Value result = interpret(prog, in);
  return std::get<int64_t>(result);
}

/// Helper: interpret program, extract bool result.
static bool run_bool(std::unique_ptr<Expr> body,
                     const std::string &input = "") {
  std::istringstream in(input);
  Program prog(std::move(body));
  Value result = interpret(prog, in);
  return std::get<bool>(result);
}

TEST(Interpreter, IntLiteral) {
  EXPECT_EQ(run(std::make_unique<IntExpr>(42)), 42);
}

TEST(Interpreter, Negation) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<IntExpr>(10));
  EXPECT_EQ(run(std::move(e)), -10);
}

TEST(Interpreter, Add) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  EXPECT_EQ(run(std::move(e)), 42);
}

TEST(Interpreter, Sub) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Sub,
      std::make_unique<IntExpr>(52),
      std::make_unique<IntExpr>(10));
  EXPECT_EQ(run(std::move(e)), 42);
}

TEST(Interpreter, NestedArith) {
  // 10 + -(12 + 20) = -22
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
  EXPECT_EQ(run(std::move(e)), -22);
}

TEST(Interpreter, LetSimple) {
  // let x = 32; 10 + x
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<IntExpr>(10),
          std::make_unique<VarExpr>("x")));
  EXPECT_EQ(run(std::move(e)), 42);
}

TEST(Interpreter, LetShadow) {
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
  EXPECT_EQ(run(std::move(e)), 42);
}

TEST(Interpreter, Read) {
  auto e = std::make_unique<ReadExpr>();
  EXPECT_EQ(run(std::move(e), "42"), 42);
}

TEST(Interpreter, ReadAdd) {
  // read() + 10
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<ReadExpr>(),
      std::make_unique<IntExpr>(10));
  EXPECT_EQ(run(std::move(e), "42"), 52);
}
