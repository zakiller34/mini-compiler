#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "passes/uncover_get.h"

TEST(UncoverGet, NoSetBangNoChange) {
    // let x = 42; x — no set!, so VarExpr stays VarExpr
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(42),
        std::make_unique<VarExpr>("x"));
    Program prog(std::move(body));
    auto result = uncover_get(prog);
    // Body should be LetExpr with VarExpr in body
    auto *le = dynamic_cast<LetExpr *>(result->body.get());
    ASSERT_NE(le, nullptr);
    auto *ve = dynamic_cast<VarExpr *>(le->body.get());
    EXPECT_NE(ve, nullptr);
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

    auto *le = dynamic_cast<LetExpr *>(result->body.get());
    ASSERT_NE(le, nullptr);
    auto *beg = dynamic_cast<BeginExpr *>(le->body.get());
    ASSERT_NE(beg, nullptr);
    ASSERT_EQ(beg->exprs.size(), 2U);
    // Second expr should be GetExpr, not VarExpr
    auto *ge = dynamic_cast<GetExpr *>(beg->exprs[1].get());
    EXPECT_NE(ge, nullptr);
    if (ge != nullptr) {
        EXPECT_EQ(ge->name, "x");
    }
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
    auto *outer = dynamic_cast<LetExpr *>(result->body.get());
    ASSERT_NE(outer, nullptr);
    auto *inner = dynamic_cast<LetExpr *>(outer->body.get());
    ASSERT_NE(inner, nullptr);
    auto *beg = dynamic_cast<BeginExpr *>(inner->body.get());
    ASSERT_NE(beg, nullptr);
    // y should still be VarExpr
    auto *ve = dynamic_cast<VarExpr *>(beg->exprs[1].get());
    EXPECT_NE(ve, nullptr);
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
    auto *le = dynamic_cast<LetExpr *>(result->body.get());
    ASSERT_NE(le, nullptr);
    auto *we = dynamic_cast<WhileExpr *>(le->body.get());
    ASSERT_NE(we, nullptr);
    auto *cmp = dynamic_cast<BinaryExpr *>(we->cond.get());
    ASSERT_NE(cmp, nullptr);
    // lhs of comparison should be GetExpr("i")
    auto *ge = dynamic_cast<GetExpr *>(cmp->lhs.get());
    EXPECT_NE(ge, nullptr);
}
