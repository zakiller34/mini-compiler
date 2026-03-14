#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ast.h"
#include "ir/c_ir.h"
#include "ir/x86_ir.h"
#include "passes/assign_homes.h"
#include "passes/emit.h"
#include "passes/explicate_control.h"
#include "passes/patch_instructions.h"
#include "passes/prelude_conclusion.h"
#include "passes/rco.h"
#include "passes/select_instructions.h"
#include "passes/uniquify.h"

/// Run full pipeline: AST -> assembly string.
static std::string run_pipeline(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto u = uniquify(prog);
  auto r = remove_complex_operands(*u);
  auto c = explicate_control(*r);
  auto s = select_instructions(c);
  auto a = assign_homes(s);
  auto p = patch_instructions(a);
  auto f = generate_prelude_conclusion(p);
  return emit(f);
}

TEST(Pipeline, IntLiteral) {
  auto asm_str = run_pipeline(std::make_unique<IntExpr>(42));
  EXPECT_FALSE(asm_str.empty());
  // Should contain movq with $42
  EXPECT_NE(asm_str.find("$42"), std::string::npos);
}

TEST(Pipeline, Add) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
  // Should contain addq
  EXPECT_NE(asm_str.find("addq"), std::string::npos);
}

TEST(Pipeline, Negation) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<IntExpr>(10));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
  EXPECT_NE(asm_str.find("negq"), std::string::npos);
}

TEST(Pipeline, LetBinding) {
  // let x = 32; x + 10
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("x"),
          std::make_unique<IntExpr>(10)));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
  // Should have main label
  EXPECT_NE(asm_str.find("main"), std::string::npos);
}

TEST(Pipeline, HasPreludeConclusion) {
  auto asm_str = run_pipeline(std::make_unique<IntExpr>(1));
  // Should contain pushq %rbp and retq
  EXPECT_NE(asm_str.find("pushq"), std::string::npos);
  EXPECT_NE(asm_str.find("retq"), std::string::npos);
}
