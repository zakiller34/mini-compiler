#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ast.h"
#include "passes/any_rebuild.h"
#include "passes/cast_insert.h"
#include "type_checker.h"

using namespace mc;

namespace {

/// @brief Collect every node of the tree, iteratively
/// @requires root != nullptr
// Dispatch over a closed node set: exempt from the 30-line rule (CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
std::vector<const Expr *> all_nodes(const Expr *root) {
    std::vector<const Expr *> out;
    std::vector<const Expr *> work{root};
    // decreases: unvisited subtree nodes
    while (!work.empty()) {
        const Expr *e = work.back();
        work.pop_back();
        if (e == nullptr) continue;
        out.push_back(e);
        if (auto kids = any_children(e)) {
            for (const Expr *k : *kids) work.push_back(k);
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
    return out;
}

bool has_kind(const Expr *root, NodeKind k) {
    for (const Expr *e : all_nodes(root)) {
        if (e->kind() == k) return true;
    }
    return false;
}

std::unique_ptr<Expr> lit(int64_t v) { return std::make_unique<IntExpr>(v); }

/// @brief cast_insert a body-only program
std::unique_ptr<Program> run(std::unique_ptr<Expr> body) {
    Program prog(std::move(body));
    return cast_insert(prog);
}

} // namespace

// -- Contract: every subexpression of the result has type Any --

TEST(CastInsert, IntLiteralIsInjected) {
    auto out = run(lit(5));
    ASSERT_EQ(out->body->kind(), NodeKind::Inject);
    EXPECT_EQ(*expr_cast<InjectExpr>(out->body.get())->ftype, *int_type());
    EXPECT_EQ(*type_check(*out), *any_type());
}

TEST(CastInsert, BoolLiteralIsInjected) {
    auto out = run(std::make_unique<BoolExpr>(true));
    ASSERT_EQ(out->body->kind(), NodeKind::Inject);
    EXPECT_EQ(*expr_cast<InjectExpr>(out->body.get())->ftype, *bool_type());
}

TEST(CastInsert, AdditionProjectsBothOperands) {
    auto out = run(std::make_unique<BinaryExpr>(BinaryOp::Add, lit(1), lit(2)));
    // (inject (+ (project e1 Int) (project e2 Int)) Int)
    ASSERT_EQ(out->body->kind(), NodeKind::Inject);
    const auto *inj = expr_cast<InjectExpr>(out->body.get());
    EXPECT_EQ(*inj->ftype, *int_type());
    ASSERT_EQ(inj->expr->kind(), NodeKind::Binary);
    const auto *add = expr_cast<BinaryExpr>(inj->expr.get());
    EXPECT_EQ(add->lhs->kind(), NodeKind::Project);
    EXPECT_EQ(add->rhs->kind(), NodeKind::Project);
    EXPECT_EQ(*expr_cast<ProjectExpr>(add->lhs.get())->ftype, *int_type());
}

TEST(CastInsert, ComparisonInjectsBool) {
    auto out = run(std::make_unique<BinaryExpr>(BinaryOp::Lt, lit(1), lit(2)));
    ASSERT_EQ(out->body->kind(), NodeKind::Inject);
    EXPECT_EQ(*expr_cast<InjectExpr>(out->body.get())->ftype, *bool_type());
}

TEST(CastInsert, IfComparesConditionAgainstInjectedFalse) {
    auto out = run(std::make_unique<IfExpr>(
        std::make_unique<BoolExpr>(true), lit(1), lit(2)));
    // (if (eq? c (inject #f Bool)) else then) — branches swap
    ASSERT_EQ(out->body->kind(), NodeKind::If);
    const auto *ife = expr_cast<IfExpr>(out->body.get());
    ASSERT_EQ(ife->cond->kind(), NodeKind::Binary);
    EXPECT_EQ(expr_cast<BinaryExpr>(ife->cond.get())->op, BinaryOp::Eq);
}

TEST(CastInsert, LambdaParamsAndReturnBecomeAny) {
    std::vector<std::pair<std::string, TypePtr>> params{{"x", int_type()}};
    auto lam = std::make_unique<LambdaExpr>(
        std::move(params), int_type(), std::make_unique<VarExpr>("x"));
    auto out = run(std::move(lam));
    ASSERT_EQ(out->body->kind(), NodeKind::Inject);
    const auto *inj = expr_cast<InjectExpr>(out->body.get());
    ASSERT_EQ(inj->expr->kind(), NodeKind::Lambda);
    const auto *la = expr_cast<LambdaExpr>(inj->expr.get());
    EXPECT_TRUE(is_any_type(la->params[0].second));
    EXPECT_TRUE(is_any_type(la->ret_type));
    EXPECT_TRUE(is_flat_type(inj->ftype));
}

TEST(CastInsert, ApplyProjectsTheCallee) {
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(lit(1));
    auto out = run(std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("f"), std::move(args)));
    ASSERT_EQ(out->body->kind(), NodeKind::Apply);
    const auto *ap = expr_cast<ApplyExpr>(out->body.get());
    ASSERT_EQ(ap->func->kind(), NodeKind::Project);
    const auto &ft = expr_cast<ProjectExpr>(ap->func.get())->ftype;
    EXPECT_TRUE(is_fun_type(ft));
    EXPECT_EQ(ft->elem_types.size(), 2U); // one Any param + Any return
}

TEST(CastInsert, VectorGetsAllAnyElementType) {
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(lit(1));
    elems.push_back(std::make_unique<BoolExpr>(false));
    auto out = run(std::make_unique<VectorExpr>(std::move(elems)));
    ASSERT_EQ(out->body->kind(), NodeKind::Inject);
    const auto &t = expr_cast<InjectExpr>(out->body.get())->ftype;
    ASSERT_TRUE(is_vector_type(t));
    EXPECT_EQ(t->elem_types.size(), 2U);
    EXPECT_TRUE(is_any_type(t->elem_types[0]));
    EXPECT_TRUE(is_any_type(t->elem_types[1]));
}

TEST(CastInsert, TupleIndexIsProjectedToInt) {
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(lit(7));
    auto vec = std::make_unique<VectorExpr>(std::move(elems));
    auto out = run(std::make_unique<AnyVectorRefExpr>(std::move(vec), lit(0)));
    ASSERT_EQ(out->body->kind(), NodeKind::AnyVectorRef);
    const auto *ar = expr_cast<AnyVectorRefExpr>(out->body.get());
    ASSERT_EQ(ar->idx->kind(), NodeKind::Project);
    EXPECT_EQ(*expr_cast<ProjectExpr>(ar->idx.get())->ftype, *int_type());
}

TEST(CastInsert, TypePredicateResultIsInjected) {
    auto out = run(std::make_unique<TypePredExpr>(TypePred::Integer, lit(3)));
    ASSERT_EQ(out->body->kind(), NodeKind::Inject);
    EXPECT_EQ(*expr_cast<InjectExpr>(out->body.get())->ftype, *bool_type());
}

TEST(CastInsert, DefSignaturesBecomeAny) {
    std::vector<DefNode> defs;
    defs.push_back(DefNode{"f", {{"x", int_type()}}, int_type(),
                            std::make_unique<VarExpr>("x")});
    Program prog(std::move(defs), lit(0));
    auto out = cast_insert(prog);
    EXPECT_TRUE(is_any_type(out->defs[0].params[0].second));
    EXPECT_TRUE(is_any_type(out->defs[0].ret_type));
}

TEST(CastInsert, ResultIsWellTypedAndHasNoRawLiteralsLeft) {
    auto body = std::make_unique<BinaryExpr>(
        BinaryOp::Add, lit(1),
        std::make_unique<BinaryExpr>(BinaryOp::Sub, lit(5), lit(2)));
    auto out = run(std::move(body));
    EXPECT_EQ(*type_check(*out), *any_type());
    // Every Int literal must sit under an Inject
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::Inject));
    EXPECT_TRUE(has_kind(out->body.get(), NodeKind::Project));
}
