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

// ---- Phase 4: while, set!, begin, void ----

TEST(Interpreter, WhileLoop) {
    // let x = 0; while (x < 3) (begin (set! x (+ x 1))); returns void, x=3
    std::vector<std::unique_ptr<Expr>> body_exprs;
    body_exprs.push_back(std::make_unique<SetBangExpr>(
        "x", std::make_unique<BinaryExpr>(
            BinaryOp::Add,
            std::make_unique<VarExpr>("x"),
            std::make_unique<IntExpr>(1))));
    auto loop = std::make_unique<WhileExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Lt,
            std::make_unique<VarExpr>("x"),
            std::make_unique<IntExpr>(3)),
        std::make_unique<BeginExpr>(std::move(body_exprs)));
    auto e = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0), std::move(loop));
    std::istringstream in("");
    Program prog(std::move(e));
    Value result = interpret(prog, in);
    // while returns void
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}

TEST(Interpreter, SetBangMutatesEnv) {
    // let x = 10; begin { set! x 42; x }
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<SetBangExpr>(
        "x", std::make_unique<IntExpr>(42)));
    bexprs.push_back(std::make_unique<VarExpr>("x"));
    auto e = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(10),
        std::make_unique<BeginExpr>(std::move(bexprs)));
    EXPECT_EQ(run(std::move(e)), 42);
}

TEST(Interpreter, BeginReturnsLast) {
    // begin { 1; 2; 42 }
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<IntExpr>(1));
    bexprs.push_back(std::make_unique<IntExpr>(2));
    bexprs.push_back(std::make_unique<IntExpr>(42));
    auto e = std::make_unique<BeginExpr>(std::move(bexprs));
    EXPECT_EQ(run(std::move(e)), 42);
}

TEST(Interpreter, VoidReturnsVoid) {
    auto e = std::make_unique<VoidExpr>();
    std::istringstream in("");
    Program prog(std::move(e));
    Value result = interpret(prog, in);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
}
