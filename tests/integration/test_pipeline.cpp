#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ast.h"
#include "ir/c_ir.h"
#include "ir/x86_ir.h"
#include "passes/assign_homes.h"
#include "passes/convert_assignments.h"
#include "passes/convert_to_closures.h"
#include "passes/emit.h"
#include "passes/explicate_control.h"
#include "passes/expose_allocation.h"
#include "passes/limit_functions.h"
#include "passes/patch_instructions.h"
#include "passes/prelude_conclusion.h"
#include "passes/rco.h"
#include "passes/reveal_functions.h"
#include "passes/select_instructions.h"
#include "passes/uncover_get.h"
#include "passes/uniquify.h"
#include "passes/shrink.h"

using namespace mc;

/// Run full pipeline: AST -> assembly string.
static std::string run_pipeline(std::unique_ptr<Expr> body,
                                std::vector<DefNode> defs = {}) {
  Program prog(std::move(defs), std::move(body));
  auto s0__ = shrink(prog); auto u = uniquify(*s0__);
  auto rf = reveal_functions(*u);
  auto lf = limit_functions(*rf);
  const auto &lf_ref = lf ? *lf : *rf;
  auto ug = uncover_get(lf_ref);
  auto ea = expose_allocation(*ug);
  auto r = remove_complex_operands(*ea);
  auto c = explicate_control(*r);
  auto s = select_instructions(c);
  auto a = assign_homes(s);
  auto p = patch_instructions(a);
  auto f = generate_prelude_conclusion(p);
  return emit(f);
}

/// Full pipeline including the closure passes — mirrors src/main.cpp exactly,
/// so lambda/closure programs lower all the way to assembly.
static std::string run_pipeline_full(std::unique_ptr<Expr> body,
                                     std::vector<DefNode> defs = {}) {
  Program prog(std::move(defs), std::move(body));
  auto s0__ = shrink(prog); auto u = uniquify(*s0__);
  auto rf = reveal_functions(*u);
  auto ca = convert_assignments(*rf);
  auto cc = convert_to_closures(*ca);
  auto lf = limit_functions(*cc);
  auto ug = uncover_get(*lf);
  auto ea = expose_allocation(*ug);
  auto r = remove_complex_operands(*ea);
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

// ---- Phase 3: end-to-end boolean/conditional tests ----

TEST(Pipeline, SimpleIf) {
  // if (1 < 2) { 42 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Lt,
          std::make_unique<IntExpr>(1),
          std::make_unique<IntExpr>(2)),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_NE(asm_str.find("cmpq"), std::string::npos);
  EXPECT_NE(asm_str.find("main"), std::string::npos);
}

TEST(Pipeline, BoolLiteral) {
  // if (true) { 42 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
}

TEST(Pipeline, NotExpr) {
  // if (not true) { 0 } else { 42 }
  // not in condition is compiled by swapping branches, no xorq
  auto e = std::make_unique<IfExpr>(
      std::make_unique<UnaryExpr>(
          UnaryOp::Not, std::make_unique<BoolExpr>(true)),
      std::make_unique<IntExpr>(0),
      std::make_unique<IntExpr>(42));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
  EXPECT_NE(asm_str.find("main"), std::string::npos);
}

TEST(Pipeline, LetCmp) {
  // let x = 10; let y = 20; if (x < y) { y - x } else { x - y }
  auto e = std::make_unique<LetExpr>(
      "x",
      std::make_unique<IntExpr>(10),
      std::make_unique<LetExpr>(
          "y",
          std::make_unique<IntExpr>(20),
          std::make_unique<IfExpr>(
              std::make_unique<BinaryExpr>(
                  BinaryOp::Lt,
                  std::make_unique<VarExpr>("x"),
                  std::make_unique<VarExpr>("y")),
              std::make_unique<BinaryExpr>(
                  BinaryOp::Sub,
                  std::make_unique<VarExpr>("y"),
                  std::make_unique<VarExpr>("x")),
              std::make_unique<BinaryExpr>(
                  BinaryOp::Sub,
                  std::make_unique<VarExpr>("x"),
                  std::make_unique<VarExpr>("y")))));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_NE(asm_str.find("cmpq"), std::string::npos);
  EXPECT_NE(asm_str.find("subq"), std::string::npos);
}

TEST(Pipeline, EqComparison) {
  // if (1 == 1) { 42 } else { 0 }
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Eq,
          std::make_unique<IntExpr>(1),
          std::make_unique<IntExpr>(1)),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_NE(asm_str.find("cmpq"), std::string::npos);
}

TEST(Pipeline, NestedIf) {
  // if (5 >= 3) { if (3 <= 5) { 42 } else { 0 } } else { 0 }
  auto inner = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Le,
          std::make_unique<IntExpr>(3),
          std::make_unique<IntExpr>(5)),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Ge,
          std::make_unique<IntExpr>(5),
          std::make_unique<IntExpr>(3)),
      std::move(inner),
      std::make_unique<IntExpr>(0));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
  EXPECT_NE(asm_str.find("cmpq"), std::string::npos);
}

// ---- Phase 4: while/set!/begin/void tests ----

TEST(Pipeline, WhileLoop) {
  // let i = 0; let sum = 0;
  // begin { while (i < 5) { begin { set! sum (sum + i); set! i (i + 1) } }; sum }
  std::vector<std::unique_ptr<Expr>> while_body;
  while_body.push_back(std::make_unique<SetBangExpr>(
      "sum", std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("sum"),
          std::make_unique<VarExpr>("i"))));
  while_body.push_back(std::make_unique<SetBangExpr>(
      "i", std::make_unique<BinaryExpr>(
          BinaryOp::Add,
          std::make_unique<VarExpr>("i"),
          std::make_unique<IntExpr>(1))));

  std::vector<std::unique_ptr<Expr>> outer;
  outer.push_back(std::make_unique<WhileExpr>(
      std::make_unique<BinaryExpr>(
          BinaryOp::Lt,
          std::make_unique<VarExpr>("i"),
          std::make_unique<IntExpr>(5)),
      std::make_unique<BeginExpr>(std::move(while_body))));
  outer.push_back(std::make_unique<VarExpr>("sum"));

  auto e = std::make_unique<LetExpr>(
      "i", std::make_unique<IntExpr>(0),
      std::make_unique<LetExpr>(
          "sum", std::make_unique<IntExpr>(0),
          std::make_unique<BeginExpr>(std::move(outer))));

  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
  EXPECT_NE(asm_str.find("main"), std::string::npos);
}

TEST(Pipeline, SetBang) {
  // let x = 10; begin { set! x 42; x }
  std::vector<std::unique_ptr<Expr>> bexprs;
  bexprs.push_back(std::make_unique<SetBangExpr>(
      "x", std::make_unique<IntExpr>(42)));
  bexprs.push_back(std::make_unique<VarExpr>("x"));
  auto e = std::make_unique<LetExpr>(
      "x", std::make_unique<IntExpr>(10),
      std::make_unique<BeginExpr>(std::move(bexprs)));
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
}

TEST(Pipeline, VoidExpr) {
  auto e = std::make_unique<VoidExpr>();
  auto asm_str = run_pipeline(std::move(e));
  EXPECT_FALSE(asm_str.empty());
}

TEST(Pipeline, NestedWhile) {
    // let i = 0; let sum = 0;
    // while (i < 3) { begin { set! sum (+ sum i); set! i (+ i 1) } };
    // sum
    std::vector<std::unique_ptr<Expr>> while_body;
    while_body.push_back(std::make_unique<SetBangExpr>(
        "sum", std::make_unique<BinaryExpr>(
            BinaryOp::Add,
            std::make_unique<VarExpr>("sum"),
            std::make_unique<VarExpr>("i"))));
    while_body.push_back(std::make_unique<SetBangExpr>(
        "i", std::make_unique<BinaryExpr>(
            BinaryOp::Add,
            std::make_unique<VarExpr>("i"),
            std::make_unique<IntExpr>(1))));

    std::vector<std::unique_ptr<Expr>> outer;
    outer.push_back(std::make_unique<WhileExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Lt,
            std::make_unique<VarExpr>("i"),
            std::make_unique<IntExpr>(3)),
        std::make_unique<BeginExpr>(std::move(while_body))));
    outer.push_back(std::make_unique<VarExpr>("sum"));

    auto e = std::make_unique<LetExpr>(
        "i", std::make_unique<IntExpr>(0),
        std::make_unique<LetExpr>(
            "sum", std::make_unique<IntExpr>(0),
            std::make_unique<BeginExpr>(std::move(outer))));

    auto asm_str = run_pipeline(std::move(e));
    EXPECT_FALSE(asm_str.empty());
    // Should have loop structure with cmpq + jmpif
    EXPECT_NE(asm_str.find("cmpq"), std::string::npos);
}

// ---- Phase 5: tuple/vector tests ----

TEST(Pipeline, SimpleTuple) {
    // let v = vector(42, 7); v[0]
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(42));
    elems.push_back(std::make_unique<IntExpr>(7));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<VectorRefExpr>(
            std::make_unique<VarExpr>("v"), 0));
    auto asm_str = run_pipeline(std::move(e));
    EXPECT_FALSE(asm_str.empty());
    EXPECT_NE(asm_str.find("main"), std::string::npos);
    // Should have GC initialization
    EXPECT_NE(asm_str.find("initialize"), std::string::npos);
}

TEST(Pipeline, TupleSet) {
    // let v = vector(1); begin { v[0] = 42; v[0] }
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<VectorSetExpr>(
        std::make_unique<VarExpr>("v"), 0,
        std::make_unique<IntExpr>(42)));
    bexprs.push_back(std::make_unique<VectorRefExpr>(
        std::make_unique<VarExpr>("v"), 0));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<BeginExpr>(std::move(bexprs)));
    auto asm_str = run_pipeline(std::move(e));
    EXPECT_FALSE(asm_str.empty());
}

TEST(Pipeline, TupleLength) {
    // let v = vector(1, 2, 3); length(v)
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    elems.push_back(std::make_unique<IntExpr>(2));
    elems.push_back(std::make_unique<IntExpr>(3));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<VectorLengthExpr>(
            std::make_unique<VarExpr>("v")));
    auto asm_str = run_pipeline(std::move(e));
    EXPECT_FALSE(asm_str.empty());
    // Should have andq and sarq for length extraction
    EXPECT_NE(asm_str.find("andq"), std::string::npos);
    EXPECT_NE(asm_str.find("sarq"), std::string::npos);
}

// ---- Phase 6: function pipeline tests ----

TEST(Pipeline, SimpleFnCall) {
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

    auto asm_str = run_pipeline(std::move(body), std::move(defs));
    EXPECT_FALSE(asm_str.empty());
    EXPECT_NE(asm_str.find("add1"), std::string::npos);
}

TEST(Pipeline, RecursiveFn) {
    // fn countdown(n: int): int { if (n == 0) 0 else countdown(n-1) + 1 }
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

    auto asm_str = run_pipeline(std::move(body), std::move(defs));
    EXPECT_FALSE(asm_str.empty());
    EXPECT_NE(asm_str.find("countdown"), std::string::npos);
}

TEST(Pipeline, MutualRecursion) {
    // fn is_even(x: int): int { if (x == 0) 1 else is_odd(x - 1) }
    // fn is_odd(x: int): int { if (x == 0) 0 else is_even(x - 1) }
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

    auto asm_str = run_pipeline(std::move(body), std::move(defs));
    EXPECT_FALSE(asm_str.empty());
    EXPECT_NE(asm_str.find("is_even"), std::string::npos);
    EXPECT_NE(asm_str.find("is_odd"), std::string::npos);
}

TEST(Pipeline, TailCall) {
    // fn loop(n: int): int { if (n == 0) 0 else loop(n - 1) }
    // Tail position call should generate TailJmp
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "loop";
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
        std::make_unique<ApplyExpr>(
            std::make_unique<VarExpr>("loop"), std::move(rec_args)));
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(10));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("loop"), std::move(args));

    auto asm_str = run_pipeline(std::move(body), std::move(defs));
    EXPECT_FALSE(asm_str.empty());
    // Tail call should use jmp * not callq *
    EXPECT_NE(asm_str.find("jmp *"), std::string::npos);
}

// ---- Phase 7: closures lower to assembly ----

// let x = 5; let f = lambda(y:Int):Int { y + x }; f(3)
// The lifted lambda is loaded via leaq; the call goes through the closure
// code pointer as an indirect call (callq *).
TEST(Pipeline, LambdaClosureLowers) {
  auto lam_body = std::make_unique<BinaryExpr>(
      BinaryOp::Add, std::make_unique<VarExpr>("y"),
      std::make_unique<VarExpr>("x"));
  std::vector<std::pair<std::string, TypePtr>> params;
  params.emplace_back("y", int_type());
  auto lam = std::make_unique<LambdaExpr>(std::move(params), int_type(),
                                          std::move(lam_body));
  std::vector<std::unique_ptr<Expr>> args;
  args.push_back(std::make_unique<IntExpr>(3));
  auto call = std::make_unique<ApplyExpr>(std::make_unique<VarExpr>("f"),
                                          std::move(args));
  auto body = std::make_unique<LetExpr>(
      "x", std::make_unique<IntExpr>(5),
      std::make_unique<LetExpr>("f", std::move(lam), std::move(call)));

  auto asm_str = run_pipeline_full(std::move(body));
  EXPECT_FALSE(asm_str.empty());
  EXPECT_NE(asm_str.find("leaq"), std::string::npos);   // lifted fn address
  EXPECT_NE(asm_str.find("callq *"), std::string::npos); // indirect call
}

// A mutable variable captured by a lambda is boxed and still lowers cleanly:
// let x = 1; let f = lambda(y:Int):Int { begin { set! x (x+y); x } }; f(10)
TEST(Pipeline, MutableCaptureLowers) {
  auto set_x = std::make_unique<SetBangExpr>(
      "x", std::make_unique<BinaryExpr>(BinaryOp::Add,
                                        std::make_unique<VarExpr>("x"),
                                        std::make_unique<VarExpr>("y")));
  std::vector<std::unique_ptr<Expr>> seq;
  seq.push_back(std::move(set_x));
  seq.push_back(std::make_unique<VarExpr>("x"));
  std::vector<std::pair<std::string, TypePtr>> params;
  params.emplace_back("y", int_type());
  auto lam = std::make_unique<LambdaExpr>(
      std::move(params), int_type(),
      std::make_unique<BeginExpr>(std::move(seq)));
  std::vector<std::unique_ptr<Expr>> args;
  args.push_back(std::make_unique<IntExpr>(10));
  auto call = std::make_unique<ApplyExpr>(std::make_unique<VarExpr>("f"),
                                          std::move(args));
  auto body = std::make_unique<LetExpr>(
      "x", std::make_unique<IntExpr>(1),
      std::make_unique<LetExpr>("f", std::move(lam), std::move(call)));

  auto asm_str = run_pipeline_full(std::move(body));
  EXPECT_FALSE(asm_str.empty());
  EXPECT_NE(asm_str.find("callq *"), std::string::npos);
}
