#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include "ast.h"
#include "interpreter.h"

using namespace mc;

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

// ---- Phase 6: function interpretation ----

TEST(Interpreter, SimpleFnCall) {
    // fn add1(x: int): int { x + 1 }; add1(41)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "add1";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<BinaryExpr>(
        BinaryOp::Add, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1));
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(41));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("add1"), std::move(args));

    std::istringstream in("");
    Program prog(std::move(defs), std::move(body));
    Value result = interpret(prog, in);
    EXPECT_EQ(std::get<int64_t>(result), 42);
}

TEST(Interpreter, RecursiveFactorial) {
    // fn fact(n: int): int { if (n == 0) 1 else n * fact(n - 1) }
    // fact(5) = 120
    // NOTE: we don't have Mul op, so use a simpler recursive fn:
    // fn countdown(n: int): int { if (n == 0) 0 else countdown(n - 1) + 1 }
    // countdown(5) = 5
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "countdown";
    d.params = {{"n", int_type()}};
    d.ret_type = int_type();
    std::vector<std::unique_ptr<Expr>> rec_args;
    rec_args.push_back(std::make_unique<BinaryExpr>(
        BinaryOp::Sub, std::make_unique<VarExpr>("n"),
        std::make_unique<IntExpr>(1)));
    d.body = std::make_unique<IfExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Eq, std::make_unique<VarExpr>("n"),
            std::make_unique<IntExpr>(0)),
        std::make_unique<IntExpr>(0),
        std::make_unique<BinaryExpr>(
            BinaryOp::Add,
            std::make_unique<ApplyExpr>(
                std::make_unique<VarExpr>("countdown"), std::move(rec_args)),
            std::make_unique<IntExpr>(1)));
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(5));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("countdown"), std::move(args));

    std::istringstream in("");
    Program prog(std::move(defs), std::move(body));
    Value result = interpret(prog, in);
    EXPECT_EQ(std::get<int64_t>(result), 5);
}

TEST(Interpreter, MutualRecursion) {
    // fn is_even(x: int): int { if (x == 0) 1 else is_odd(x - 1) }
    // fn is_odd(x: int): int { if (x == 0) 0 else is_even(x - 1) }
    // is_even(4)
    std::vector<DefNode> defs;

    DefNode d1;
    d1.name = "is_even";
    d1.params = {{"x", int_type()}};
    d1.ret_type = int_type();
    std::vector<std::unique_ptr<Expr>> odd_args;
    odd_args.push_back(std::make_unique<BinaryExpr>(
        BinaryOp::Sub, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1)));
    d1.body = std::make_unique<IfExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Eq, std::make_unique<VarExpr>("x"),
            std::make_unique<IntExpr>(0)),
        std::make_unique<IntExpr>(1),
        std::make_unique<ApplyExpr>(
            std::make_unique<VarExpr>("is_odd"), std::move(odd_args)));
    defs.push_back(std::move(d1));

    DefNode d2;
    d2.name = "is_odd";
    d2.params = {{"x", int_type()}};
    d2.ret_type = int_type();
    std::vector<std::unique_ptr<Expr>> even_args;
    even_args.push_back(std::make_unique<BinaryExpr>(
        BinaryOp::Sub, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1)));
    d2.body = std::make_unique<IfExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Eq, std::make_unique<VarExpr>("x"),
            std::make_unique<IntExpr>(0)),
        std::make_unique<IntExpr>(0),
        std::make_unique<ApplyExpr>(
            std::make_unique<VarExpr>("is_even"), std::move(even_args)));
    defs.push_back(std::move(d2));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(4));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("is_even"), std::move(args));

    std::istringstream in("");
    Program prog(std::move(defs), std::move(body));
    Value result = interpret(prog, in);
    EXPECT_EQ(std::get<int64_t>(result), 1);
}
