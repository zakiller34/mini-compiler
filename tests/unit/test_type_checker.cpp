#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "type.h"
#include "type_checker.h"

using namespace mc;

/// Helper: type-check a single expression, return TypeKind.
static TypeKind check(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  return type_check(prog)->kind;
}

/// Helper: type-check and return full TypePtr.
static TypePtr check_full(std::unique_ptr<Expr> body) {
  Program prog(std::move(body));
  return type_check(prog);
}

// ---- Well-typed accepts ----

TEST(TypeChecker, IntArith) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(10),
      std::make_unique<IntExpr>(32));
  EXPECT_EQ(check(std::move(e)), TypeKind::Int);
}

TEST(TypeChecker, IntSub) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Sub,
      std::make_unique<IntExpr>(52),
      std::make_unique<IntExpr>(10));
  EXPECT_EQ(check(std::move(e)), TypeKind::Int);
}

TEST(TypeChecker, BoolLiteral) {
  EXPECT_EQ(check(std::make_unique<BoolExpr>(true)), TypeKind::Bool);
}

TEST(TypeChecker, NotBool) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Not, std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, BoolLogicAnd) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::And,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, BoolLogicOr) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Or,
      std::make_unique<BoolExpr>(false),
      std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, IfExpr) {
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(0));
  EXPECT_EQ(check(std::move(e)), TypeKind::Int);
}

TEST(TypeChecker, IfBoolBranches) {
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false),
      std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, ComparisonLt) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Lt,
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(2));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, ComparisonGe) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Ge,
      std::make_unique<IntExpr>(5),
      std::make_unique<IntExpr>(3));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, EqInt) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Eq,
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(1));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, EqBool) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Eq,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(true));
  EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, LetWithIf) {
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
  EXPECT_EQ(check(std::move(e)), TypeKind::Int);
}

TEST(TypeChecker, NegInt) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<IntExpr>(10));
  EXPECT_EQ(check(std::move(e)), TypeKind::Int);
}

// ---- Ill-typed rejects ----

TEST(TypeChecker, NotIntThrows) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Not, std::make_unique<IntExpr>(42));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, AddBoolThrows) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<IntExpr>(1),
      std::make_unique<BoolExpr>(true));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, IfCondNotBoolThrows) {
  auto e = std::make_unique<IfExpr>(
      std::make_unique<IntExpr>(42),
      std::make_unique<IntExpr>(1),
      std::make_unique<IntExpr>(0));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, IfBranchMismatchThrows) {
  auto e = std::make_unique<IfExpr>(
      std::make_unique<BoolExpr>(true),
      std::make_unique<IntExpr>(42),
      std::make_unique<BoolExpr>(false));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, CmpBoolThrows) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Lt,
      std::make_unique<IntExpr>(1),
      std::make_unique<BoolExpr>(true));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, AddBoolsThrows) {
  auto e = std::make_unique<BinaryExpr>(
      BinaryOp::Add,
      std::make_unique<BoolExpr>(true),
      std::make_unique<BoolExpr>(false));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, NegBoolThrows) {
  auto e = std::make_unique<UnaryExpr>(
      UnaryOp::Neg, std::make_unique<BoolExpr>(true));
  EXPECT_THROW(check(std::move(e)), TypeError);
}

// ---- Phase 4: while, set!, begin, void ----

TEST(TypeChecker, WhileIsVoid) {
    auto e = std::make_unique<WhileExpr>(
        std::make_unique<BoolExpr>(true),
        std::make_unique<IntExpr>(42));
    EXPECT_EQ(check(std::move(e)), TypeKind::Void);
}

TEST(TypeChecker, WhileCondMustBeBool) {
    auto e = std::make_unique<WhileExpr>(
        std::make_unique<IntExpr>(42),
        std::make_unique<IntExpr>(1));
    EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, SetBangIsVoid) {
    auto e = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(10),
        std::make_unique<SetBangExpr>(
            "x", std::make_unique<IntExpr>(42)));
    EXPECT_EQ(check(std::move(e)), TypeKind::Void);
}

TEST(TypeChecker, SetBangUnboundThrows) {
    auto e = std::make_unique<SetBangExpr>(
        "x", std::make_unique<IntExpr>(42));
    EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, SetBangTypeMismatchThrows) {
    auto e = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(10),
        std::make_unique<SetBangExpr>(
            "x", std::make_unique<BoolExpr>(true)));
    EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, BeginTypeIsLast) {
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<IntExpr>(1));
    bexprs.push_back(std::make_unique<BoolExpr>(true));
    auto e = std::make_unique<BeginExpr>(std::move(bexprs));
    EXPECT_EQ(check(std::move(e)), TypeKind::Bool);
}

TEST(TypeChecker, VoidIsVoid) {
    EXPECT_EQ(check(std::make_unique<VoidExpr>()), TypeKind::Void);
}

// ---- Phase 5: vector types ----

TEST(TypeChecker, VectorWellTyped) {
    // vector(1, 2)
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    elems.push_back(std::make_unique<IntExpr>(2));
    auto t = check_full(std::make_unique<VectorExpr>(std::move(elems)));
    EXPECT_EQ(t->kind, TypeKind::Vector);
    EXPECT_EQ(t->elem_types.size(), 2u);
    EXPECT_EQ(*t->elem_types[0], *int_type());
    EXPECT_EQ(*t->elem_types[1], *int_type());
}

TEST(TypeChecker, VectorRefType) {
    // let v = vector(1, true); v[0] => Int
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    elems.push_back(std::make_unique<BoolExpr>(true));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<VectorRefExpr>(
            std::make_unique<VarExpr>("v"), 0));
    EXPECT_EQ(check(std::move(e)), TypeKind::Int);
}

TEST(TypeChecker, VectorSetIsVoid) {
    // let v = vector(1); v[0] = 2 => Void
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<VectorSetExpr>(
            std::make_unique<VarExpr>("v"), 0,
            std::make_unique<IntExpr>(2)));
    EXPECT_EQ(check(std::move(e)), TypeKind::Void);
}

TEST(TypeChecker, VectorLengthIsInt) {
    // let v = vector(1, 2); length(v) => Int
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    elems.push_back(std::make_unique<IntExpr>(2));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<VectorLengthExpr>(
            std::make_unique<VarExpr>("v")));
    EXPECT_EQ(check(std::move(e)), TypeKind::Int);
}

TEST(TypeChecker, VectorRefOOBThrows) {
    // let v = vector(1); v[1] => TypeError
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<VectorRefExpr>(
            std::make_unique<VarExpr>("v"), 1));
    EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, VectorSetTypeMismatchThrows) {
    // let v = vector(1); v[0] = true => TypeError
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    auto e = std::make_unique<LetExpr>(
        "v", std::make_unique<VectorExpr>(std::move(elems)),
        std::make_unique<VectorSetExpr>(
            std::make_unique<VarExpr>("v"), 0,
            std::make_unique<BoolExpr>(true)));
    EXPECT_THROW(check(std::move(e)), TypeError);
}

TEST(TypeChecker, NestedVector) {
    // vector(vector(1))
    std::vector<std::unique_ptr<Expr>> inner;
    inner.push_back(std::make_unique<IntExpr>(1));
    std::vector<std::unique_ptr<Expr>> outer;
    outer.push_back(std::make_unique<VectorExpr>(std::move(inner)));
    auto t = check_full(std::make_unique<VectorExpr>(std::move(outer)));
    EXPECT_EQ(t->kind, TypeKind::Vector);
    EXPECT_EQ(t->elem_types[0]->kind, TypeKind::Vector);
}

// ---- Phase 6: function type checking ----

TEST(TypeChecker, FnDefBodyType) {
    // fn foo(x: int): int { x + 1 }; foo(42) => Int
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<BinaryExpr>(
        BinaryOp::Add, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1));
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(42));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("foo"), std::move(args));

    Program prog(std::move(defs), std::move(body));
    auto t = type_check(prog);
    EXPECT_EQ(t->kind, TypeKind::Int);
}

TEST(TypeChecker, FnArgCountMismatchThrows) {
    // fn foo(x: int): int { x }; foo(1, 2) => TypeError
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(1));
    args.push_back(std::make_unique<IntExpr>(2));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("foo"), std::move(args));

    Program prog(std::move(defs), std::move(body));
    EXPECT_THROW(type_check(prog), TypeError);
}

TEST(TypeChecker, FnArgTypeMismatchThrows) {
    // fn foo(x: int): int { x }; foo(true) => TypeError
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<BoolExpr>(true));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("foo"), std::move(args));

    Program prog(std::move(defs), std::move(body));
    EXPECT_THROW(type_check(prog), TypeError);
}

TEST(TypeChecker, FnReturnTypeMismatchThrows) {
    // fn foo(x: int): bool { x + 1 } => TypeError (body returns int, declared bool)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = bool_type();
    d.body = std::make_unique<BinaryExpr>(
        BinaryOp::Add, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1));
    defs.push_back(std::move(d));

    auto body = std::make_unique<IntExpr>(0);
    Program prog(std::move(defs), std::move(body));
    EXPECT_THROW(type_check(prog), TypeError);
}

TEST(TypeChecker, MutualRecursionTypechecks) {
    // fn is_even(x: int): bool { if (x == 0) true else is_odd(x - 1) }
    // fn is_odd(x: int): bool { if (x == 0) false else is_even(x - 1) }
    // is_even(4)
    std::vector<DefNode> defs;

    // is_even
    DefNode d1;
    d1.name = "is_even";
    d1.params = {{"x", int_type()}};
    d1.ret_type = bool_type();
    std::vector<std::unique_ptr<Expr>> odd_args;
    odd_args.push_back(std::make_unique<BinaryExpr>(
        BinaryOp::Sub, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1)));
    d1.body = std::make_unique<IfExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Eq, std::make_unique<VarExpr>("x"),
            std::make_unique<IntExpr>(0)),
        std::make_unique<BoolExpr>(true),
        std::make_unique<ApplyExpr>(
            std::make_unique<VarExpr>("is_odd"), std::move(odd_args)));
    defs.push_back(std::move(d1));

    // is_odd
    DefNode d2;
    d2.name = "is_odd";
    d2.params = {{"x", int_type()}};
    d2.ret_type = bool_type();
    std::vector<std::unique_ptr<Expr>> even_args;
    even_args.push_back(std::make_unique<BinaryExpr>(
        BinaryOp::Sub, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1)));
    d2.body = std::make_unique<IfExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Eq, std::make_unique<VarExpr>("x"),
            std::make_unique<IntExpr>(0)),
        std::make_unique<BoolExpr>(false),
        std::make_unique<ApplyExpr>(
            std::make_unique<VarExpr>("is_even"), std::move(even_args)));
    defs.push_back(std::move(d2));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(4));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("is_even"), std::move(args));

    Program prog(std::move(defs), std::move(body));
    auto t = type_check(prog);
    EXPECT_EQ(t->kind, TypeKind::Bool);
}
