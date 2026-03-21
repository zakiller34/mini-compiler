#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ast.h"

using namespace mc;

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

// ---- Phase 6: function AST dump tests ----

TEST(ParserDump, FunRefDump) {
    auto e = std::make_unique<FunRefExpr>("foo", 2);
    EXPECT_EQ(e->dump(), "(fun-ref foo 2)");
}

TEST(ParserDump, ApplyDump) {
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(1));
    args.push_back(std::make_unique<IntExpr>(2));
    auto e = std::make_unique<ApplyExpr>(
        std::make_unique<FunRefExpr>("add", 2), std::move(args));
    EXPECT_EQ(e->dump(), "(apply (fun-ref add 2) 1 2)");
}

TEST(ParserDump, DefNodeDump) {
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    // Check dump does not crash and produces non-empty output
    EXPECT_FALSE(d.dump().empty());
}
