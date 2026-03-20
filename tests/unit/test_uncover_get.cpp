#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "passes/uncover_get.h"

using namespace mc;

TEST(UncoverGet, NoSetBangNoChange) {
    // let x = 42; x — no set!, so VarExpr stays VarExpr
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(42),
        std::make_unique<VarExpr>("x"));
    Program prog(std::move(body));
    auto result = uncover_get(prog);
    // Body should be LetExpr with VarExpr in body
    ASSERT_EQ(result->body->kind(), NodeKind::Let);
    auto *le = expr_cast<LetExpr>(result->body.get());
    EXPECT_EQ(le->body->kind(), NodeKind::Var);
}

TEST(UncoverGet, SetBangConvertsToGet) {
    // let x = 0; begin { set! x 42; x }
    // x is target of set!, so VarExpr(x) → GetExpr(x)
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<SetBangExpr>(
        "x", std::make_unique<IntExpr>(42)));
    bexprs.push_back(std::make_unique<VarExpr>("x"));
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0),
        std::make_unique<BeginExpr>(std::move(bexprs)));
    Program prog(std::move(body));
    auto result = uncover_get(prog);

    ASSERT_EQ(result->body->kind(), NodeKind::Let);
    auto *le = expr_cast<LetExpr>(result->body.get());
    ASSERT_EQ(le->body->kind(), NodeKind::Begin);
    auto *beg = expr_cast<BeginExpr>(le->body.get());
    ASSERT_EQ(beg->exprs.size(), 2U);
    // Second expr should be GetExpr, not VarExpr
    ASSERT_EQ(beg->exprs[1]->kind(), NodeKind::Get);
    auto *ge = expr_cast<GetExpr>(beg->exprs[1].get());
    EXPECT_EQ(ge->name, "x");
}

TEST(UncoverGet, NonMutableVarUnchanged) {
    // let x = 0; let y = 1; begin { set! x 42; y }
    // y is NOT a set! target, stays VarExpr
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<SetBangExpr>(
        "x", std::make_unique<IntExpr>(42)));
    bexprs.push_back(std::make_unique<VarExpr>("y"));
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0),
        std::make_unique<LetExpr>(
            "y", std::make_unique<IntExpr>(1),
            std::make_unique<BeginExpr>(std::move(bexprs))));
    Program prog(std::move(body));
    auto result = uncover_get(prog);

    // Navigate to the begin's second expr
    ASSERT_EQ(result->body->kind(), NodeKind::Let);
    auto *outer = expr_cast<LetExpr>(result->body.get());
    ASSERT_EQ(outer->body->kind(), NodeKind::Let);
    auto *inner = expr_cast<LetExpr>(outer->body.get());
    ASSERT_EQ(inner->body->kind(), NodeKind::Begin);
    auto *beg = expr_cast<BeginExpr>(inner->body.get());
    // y should still be VarExpr
    EXPECT_EQ(beg->exprs[1]->kind(), NodeKind::Var);
}

TEST(UncoverGet, WhileBodyConverted) {
    // let i = 0; while (i < 10) { set! i (i + 1) }
    // i is target of set!, so VarExpr(i) → GetExpr(i) in condition too
    auto body = std::make_unique<LetExpr>(
        "i", std::make_unique<IntExpr>(0),
        std::make_unique<WhileExpr>(
            std::make_unique<BinaryExpr>(
                BinaryOp::Lt,
                std::make_unique<VarExpr>("i"),
                std::make_unique<IntExpr>(10)),
            std::make_unique<SetBangExpr>(
                "i",
                std::make_unique<BinaryExpr>(
                    BinaryOp::Add,
                    std::make_unique<VarExpr>("i"),
                    std::make_unique<IntExpr>(1)))));
    Program prog(std::move(body));
    auto result = uncover_get(prog);

    // Navigate to while condition
    ASSERT_EQ(result->body->kind(), NodeKind::Let);
    auto *le = expr_cast<LetExpr>(result->body.get());
    ASSERT_EQ(le->body->kind(), NodeKind::While);
    auto *we = expr_cast<WhileExpr>(le->body.get());
    ASSERT_EQ(we->cond->kind(), NodeKind::Binary);
    auto *cmp = expr_cast<BinaryExpr>(we->cond.get());
    // lhs of comparison should be GetExpr("i")
    EXPECT_EQ(cmp->lhs->kind(), NodeKind::Get);
}
