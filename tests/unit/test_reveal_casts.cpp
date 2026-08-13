#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ast.h"
#include "passes/any_rebuild.h"
#include "passes/cast_insert.h"
#include "passes/reveal_casts.h"

using namespace mc;

namespace {

/// @brief Iteratively test whether any node of the tree has kind `k`
/// @requires root != nullptr
// Dispatch over a closed node set: exempt from the 30-line rule (CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
bool has_kind(const Expr *root, NodeKind k) {
    std::vector<const Expr *> work{root};
    // decreases: unvisited subtree nodes
    while (!work.empty()) {
        const Expr *e = work.back();
        work.pop_back();
        if (e == nullptr) continue;
        if (e->kind() == k) return true;
        if (auto kids = any_children(e)) {
            for (const Expr *c : *kids) work.push_back(c);
            continue;
        }
        switch (e->kind()) {
        case NodeKind::Unary:
            work.push_back(expr_cast<UnaryExpr>(e)->operand.get());
            break;
        case NodeKind::Binary:
            work.push_back(expr_cast<BinaryExpr>(e)->lhs.get());
            work.push_back(expr_cast<BinaryExpr>(e)->rhs.get());
            break;
        case NodeKind::If:
            work.push_back(expr_cast<IfExpr>(e)->cond.get());
            work.push_back(expr_cast<IfExpr>(e)->then_branch.get());
            work.push_back(expr_cast<IfExpr>(e)->else_branch.get());
            break;
        case NodeKind::Let:
            work.push_back(expr_cast<LetExpr>(e)->init.get());
            work.push_back(expr_cast<LetExpr>(e)->body.get());
            break;
        case NodeKind::Lambda:
            work.push_back(expr_cast<LambdaExpr>(e)->body.get());
            break;
        case NodeKind::Apply:
            work.push_back(expr_cast<ApplyExpr>(e)->func.get());
            for (const auto &a : expr_cast<ApplyExpr>(e)->args) {
                work.push_back(a.get());
            }
            break;
        case NodeKind::Vector:
            for (const auto &el : expr_cast<VectorExpr>(e)->elems) {
                work.push_back(el.get());
            }
            break;
        default:
            break;
        }
    }
    return false;
}

std::unique_ptr<Expr> lit(int64_t v) { return std::make_unique<IntExpr>(v); }

std::unique_ptr<Program> reveal(std::unique_ptr<Expr> body) {
    Program prog(std::move(body));
    return reveal_casts(prog);
}

} // namespace

// -- Contract: no Inject/Project/TypePredicate survives --

TEST(RevealCasts, InjectBecomesMakeAnyWithTheRightTag) {
    auto out = reveal(std::make_unique<InjectExpr>(lit(7), int_type()));
    ASSERT_EQ(out->body->kind(), NodeKind::MakeAny);
    EXPECT_EQ(expr_cast<MakeAnyExpr>(out->body.get())->tag, kTagInt);
    EXPECT_FALSE(has_kind(out->body.get(), NodeKind::Inject));
}

TEST(RevealCasts, InjectBoolUsesBoolTag) {
    auto out = reveal(std::make_unique<InjectExpr>(
        std::make_unique<BoolExpr>(true), bool_type()));
    EXPECT_EQ(expr_cast<MakeAnyExpr>(out->body.get())->tag, kTagBool);
}

TEST(RevealCasts, ProjectBecomesTagTestValueOfOrExit) {
    auto out = reveal(std::make_unique<ProjectExpr>(
        std::make_unique<VarExpr>("a"), int_type()));
    // (let tmp a (if (eq? (tag-of-any tmp) 1) (value-of tmp Int) (exit)))
    ASSERT_EQ(out->body->kind(), NodeKind::Let);
    const auto *le = expr_cast<LetExpr>(out->body.get());
    ASSERT_EQ(le->body->kind(), NodeKind::If);
    const auto *ife = expr_cast<IfExpr>(le->body.get());
    ASSERT_EQ(ife->cond->kind(), NodeKind::Binary);
    EXPECT_TRUE(has_kind(ife->cond.get(), NodeKind::TagOfAny));
    EXPECT_EQ(ife->then_branch->kind(), NodeKind::ValueOf);
    EXPECT_EQ(ife->else_branch->kind(), NodeKind::Exit);
    EXPECT_FALSE(has_kind(out->body.get(), NodeKind::Project));
}

TEST(RevealCasts, ProjectToTupleAlsoChecksTheLength) {
    auto vec_t = vector_type({any_type(), any_type()});
    auto out = reveal(std::make_unique<ProjectExpr>(
        std::make_unique<VarExpr>("a"), vec_t));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::AnyVectorLength));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::Exit));
}

TEST(RevealCasts, ProjectToProcedureAlsoChecksTheArity) {
    auto fn_t = fun_type({any_type()}, any_type());
    auto out = reveal(std::make_unique<ProjectExpr>(
        std::make_unique<VarExpr>("f"), fn_t));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::ProcArity));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::Exit));
}

TEST(RevealCasts, TypePredicateBecomesATagComparison) {
    auto out = reveal(std::make_unique<TypePredExpr>(
        TypePred::Vector, std::make_unique<VarExpr>("a")));
    ASSERT_EQ(out->body->kind(), NodeKind::Binary);
    const auto *cmp = expr_cast<BinaryExpr>(out->body.get());
    EXPECT_EQ(cmp->op, BinaryOp::Eq);
    EXPECT_EQ(cmp->lhs->kind(), NodeKind::TagOfAny);
    ASSERT_EQ(cmp->rhs->kind(), NodeKind::Int);
    EXPECT_EQ(expr_cast<IntExpr>(cmp->rhs.get())->value, kTagVector);
    EXPECT_FALSE(has_kind(out->body.get(), NodeKind::TypePredicate));
}

TEST(RevealCasts, EveryPredicateGetsItsOwnTagCode) {
    struct Case { TypePred pred; int64_t tag; };
    const Case cases[] = {{TypePred::Integer, kTagInt},
                          {TypePred::Boolean, kTagBool},
                          {TypePred::Vector, kTagVector},
                          {TypePred::Procedure, kTagFunction},
                          {TypePred::Void, kTagVoid}};
    // invariant: cases[0..i) lowered to their own tag code
    for (const auto &c : cases) {
        auto out = reveal(std::make_unique<TypePredExpr>(
            c.pred, std::make_unique<VarExpr>("a")));
        const auto *cmp = expr_cast<BinaryExpr>(out->body.get());
        EXPECT_EQ(expr_cast<IntExpr>(cmp->rhs.get())->value, c.tag);
    }
}

TEST(RevealCasts, AnyVectorRefGetsATagAndBoundsCheck) {
    auto out = reveal(std::make_unique<AnyVectorRefExpr>(
        std::make_unique<VarExpr>("v"), std::make_unique<VarExpr>("i")));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::TagOfAny));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::AnyVectorLength));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::Exit));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::AnyVectorRef));
}

TEST(RevealCasts, AnyVectorSetGetsATagAndBoundsCheck) {
    auto out = reveal(std::make_unique<AnyVectorSetExpr>(
        std::make_unique<VarExpr>("v"), std::make_unique<VarExpr>("i"),
        std::make_unique<VarExpr>("x")));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::AnyVectorLength));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::Exit));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::AnyVectorSet));
}

TEST(RevealCasts, CastInsertOutputIsFullyRevealed) {
    Program prog(std::make_unique<BinaryExpr>(BinaryOp::Add, lit(1), lit(2)));
    auto casted = cast_insert(prog);
    auto out = reveal_casts(*casted);
    EXPECT_FALSE(has_kind(out->body.get(), NodeKind::Inject));
    EXPECT_FALSE(has_kind(out->body.get(), NodeKind::Project));
    EXPECT_FALSE(has_kind(out->body.get(), NodeKind::TypePredicate));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::MakeAny));
}

TEST(RevealCasts, NonCastNodesArePreserved) {
    auto out = reveal(std::make_unique<LetExpr>(
        "x", lit(1),
        std::make_unique<BinaryExpr>(BinaryOp::Add,
                                      std::make_unique<VarExpr>("x"),
                                      lit(2))));
    EXPECT_EQ(out->body->kind(), NodeKind::Let);
    EXPECT_EQ(expr_cast<LetExpr>(out->body.get())->var, "x");
}
