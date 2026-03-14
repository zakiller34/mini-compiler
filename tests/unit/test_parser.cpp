#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ast.h"

// Test AST construction + dump() output (S-expr format).
// Builds ASTs manually — no parser dependency.

TEST(ParserDump, IntLiteral) {
  auto e = std::make_unique<IntExpr>(42);
  EXPECT_EQ(e->dump(), "42");
}

TEST(ParserDump, Negation) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<IntExpr>(10));
  EXPECT_EQ(e->dump(), "(- 10)");
}

TEST(ParserDump, Add) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  EXPECT_EQ(e->dump(), "(+ 10 32)");
}

TEST(ParserDump, Sub) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Sub,
      std::make_unique<IntExpr>(52),
      std::make_unique<IntExpr>(10));
  EXPECT_EQ(e->dump(), "(- 52 10)");
}

TEST(ParserDump, Read) {
  auto e = std::make_unique<ReadExpr>();
  EXPECT_EQ(e->dump(), "(read)");
}

TEST(ParserDump, Var) {
  auto e = std::make_unique<VarExpr>("x");
  EXPECT_EQ(e->dump(), "x");
}

TEST(ParserDump, LetSimple) {
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<IntExpr>(10),
          std::make_unique<VarExpr>("x")));
  EXPECT_EQ(e->dump(), "(let ([x 32]) (+ 10 x))");
}

TEST(ParserDump, NestedLet) {
  // let x = 5; let y = 37; x + y
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
  EXPECT_EQ(e->dump(), "(let ([x 5]) (let ([y 37]) (+ x y)))");
}

TEST(ParserDump, ProgramWrap) {
  auto prog = Program(std::make_unique<IntExpr>(42));
  EXPECT_EQ(prog.dump(), "(program 42)");
}
