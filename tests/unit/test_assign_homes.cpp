#include <gtest/gtest.h>

#include <memory>
#include <variant>
#include <vector>

#include "ast.h"
#include "ir/x86_ir.h"
#include "passes/assign_homes.h"
#include "passes/explicate_control.h"
#include "passes/rco.h"
#include "passes/select_instructions.h"
#include "passes/uniquify.h"

/// Helper: run pipeline through assign_homes.
static x86::X86Program run_assign(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  auto u = uniquify(prog);
  auto r = remove_complex_operands(*u);
  auto c = explicate_control(*r);
  auto s = select_instructions(c);
  return assign_homes(s);
}

/// Check no VarArg remains in any instruction arg.
static bool no_var_args(const x86::X86Program &prog) {
  // invariant all checked blocks had no VarArg
  for (const auto &[label, blk] : prog.blocks) {
    // invariant all checked instrs in blk had no VarArg
    for (const auto &instr : blk.instrs) {
      auto check_arg = [](const x86::Arg &a) {
        return !std::holds_alternative<x86::VarArg>(a);
      };

      if (std::holds_alternative<x86::Movq>(instr)) {
        const auto &m = std::get<x86::Movq>(instr);
        if (!check_arg(m.src) || !check_arg(m.dst)) return false;
      } else if (std::holds_alternative<x86::Addq>(instr)) {
        const auto &a = std::get<x86::Addq>(instr);
        if (!check_arg(a.src) || !check_arg(a.dst)) return false;
      } else if (std::holds_alternative<x86::Subq>(instr)) {
        const auto &s = std::get<x86::Subq>(instr);
        if (!check_arg(s.src) || !check_arg(s.dst)) return false;
      } else if (std::holds_alternative<x86::Negq>(instr)) {
        const auto &n = std::get<x86::Negq>(instr);
        if (!check_arg(n.dst)) return false;
      }
    }
  }
  return true;
}

TEST(AssignHomes, IntLiteralNoVars) {
  auto prog = run_assign(std::make_unique<IntExpr>(42));
  EXPECT_TRUE(no_var_args(prog));
}

TEST(AssignHomes, LetBindingNoVars) {
  // let x = 32; x + 10
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(32),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("x"),
          std::make_unique<IntExpr>(10)));
  auto prog = run_assign(std::move(e));
  EXPECT_TRUE(no_var_args(prog));
}

TEST(AssignHomes, StackSpacePositive) {
  // let x = 5; let y = 10; x + y — needs stack space
  auto inner = std::make_unique<LetExpr>(
      "y",
      std::make_unique<IntExpr>(10),
      std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("x"),
          std::make_unique<VarExpr>("y")));
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(5),
      std::move(inner));
  auto prog = run_assign(std::move(e));
  EXPECT_TRUE(no_var_args(prog));
  EXPECT_GT(prog.stack_space, 0);
}
