#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "ast.h"
#include "passes/convert_assignments.h"

using namespace mc;

namespace {

/// Build `lambda(y:Int):Int { <body> }`.
std::unique_ptr<Expr> lam_y(std::unique_ptr<Expr> body) {
    std::vector<std::pair<std::string, TypePtr>> params;
    params.emplace_back("y", int_type());
    return std::make_unique<LambdaExpr>(std::move(params), int_type(),
                                        std::move(body));
}

} // namespace

// A var that is BOTH set! and captured by a lambda is boxed into a 1-tuple:
//   let x = 0; begin { set! x 5; lambda(y){ x } }
// -> let x = vector(0); begin { x[0] = 5; lambda(y){ x[0] } }
TEST(ConvertAssignments, BoxesAssignedAndCaptured) {
    std::vector<std::unique_ptr<Expr>> seq;
    seq.push_back(std::make_unique<SetBangExpr>("x", std::make_unique<IntExpr>(5)));
    seq.push_back(lam_y(std::make_unique<VarExpr>("x")));
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0),
        std::make_unique<BeginExpr>(std::move(seq)));

    Program prog(std::move(body));
    auto out = convert_assignments(prog);
    ASSERT_NE(out, nullptr);

    // Binding init is boxed: let x = vector(0)
    ASSERT_EQ(out->body->kind(), NodeKind::Let);
    const auto *let = expr_cast<LetExpr>(out->body.get());
    EXPECT_EQ(let->init->kind(), NodeKind::Vector);

    ASSERT_EQ(let->body->kind(), NodeKind::Begin);
    const auto *beg = expr_cast<BeginExpr>(let->body.get());
    // set! x 5  ->  x[0] = 5
    EXPECT_EQ(beg->exprs[0]->kind(), NodeKind::VectorSet);
    // lambda body read of x  ->  x[0]
    ASSERT_EQ(beg->exprs[1]->kind(), NodeKind::Lambda);
    const auto *lam = expr_cast<LambdaExpr>(beg->exprs[1].get());
    EXPECT_EQ(lam->body->kind(), NodeKind::VectorRef);
}

// set! but NOT captured -> not boxed (stays a plain SetBang / Let(Int)).
TEST(ConvertAssignments, SetBangNotCapturedNotBoxed) {
    std::vector<std::unique_ptr<Expr>> seq;
    seq.push_back(std::make_unique<SetBangExpr>("x", std::make_unique<IntExpr>(5)));
    seq.push_back(std::make_unique<VarExpr>("x"));
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0),
        std::make_unique<BeginExpr>(std::move(seq)));

    Program prog(std::move(body));
    auto out = convert_assignments(prog);
    ASSERT_EQ(out->body->kind(), NodeKind::Let);
    const auto *let = expr_cast<LetExpr>(out->body.get());
    EXPECT_EQ(let->init->kind(), NodeKind::Int); // not boxed
    const auto *beg = expr_cast<BeginExpr>(let->body.get());
    EXPECT_EQ(beg->exprs[0]->kind(), NodeKind::SetBang); // unchanged
    EXPECT_EQ(beg->exprs[1]->kind(), NodeKind::Var);     // unchanged
}

// Captured but NEVER set! -> not boxed (immutable capture needs no box).
TEST(ConvertAssignments, CapturedNotAssignedNotBoxed) {
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0), lam_y(std::make_unique<VarExpr>("x")));

    Program prog(std::move(body));
    auto out = convert_assignments(prog);
    ASSERT_EQ(out->body->kind(), NodeKind::Let);
    const auto *let = expr_cast<LetExpr>(out->body.get());
    EXPECT_EQ(let->init->kind(), NodeKind::Int); // not boxed
    const auto *lam = expr_cast<LambdaExpr>(let->body.get());
    EXPECT_EQ(lam->body->kind(), NodeKind::Var); // read unchanged
}
