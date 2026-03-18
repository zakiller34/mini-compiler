#include <gtest/gtest.h>
#include <z3.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ast.h"
#include "passes/uncover_get.h"

/// @brief Z3 predicate: uncover_get ensures no VarExpr for set! targets
/// For each mutable var v, assert "VarExpr(v) exists in output" and check UNSAT.
static bool z3_no_var_for_mutable(const Program &result,
                                   const std::set<std::string> &mvars) {
    Z3_config cfg = Z3_mk_config();
    Z3_context ctx = Z3_mk_context(cfg);
    Z3_del_config(cfg);

    // Walk the output tree, check no VarExpr has name in mvars
    std::vector<const Expr *> worklist;
    worklist.push_back(result.body.get());
    bool found_violation = false;

    // invariant: no violation found in visited nodes
    while (!worklist.empty()) {
        const Expr *e = worklist.back();
        worklist.pop_back();

        switch (e->kind()) {
        case NodeKind::Var:
            if (mvars.count(static_cast<const VarExpr *>(e)->name) != 0U) {
                found_violation = true;
            }
            break;
        case NodeKind::Unary:
            worklist.push_back(
                static_cast<const UnaryExpr *>(e)->operand.get());
            break;
        case NodeKind::Binary: {
            auto *be = static_cast<const BinaryExpr *>(e);
            worklist.push_back(be->lhs.get());
            worklist.push_back(be->rhs.get());
            break;
        }
        case NodeKind::If: {
            auto *ife = static_cast<const IfExpr *>(e);
            worklist.push_back(ife->cond.get());
            worklist.push_back(ife->then_branch.get());
            worklist.push_back(ife->else_branch.get());
            break;
        }
        case NodeKind::Let: {
            auto *le = static_cast<const LetExpr *>(e);
            worklist.push_back(le->init.get());
            worklist.push_back(le->body.get());
            break;
        }
        case NodeKind::While: {
            auto *we = static_cast<const WhileExpr *>(e);
            worklist.push_back(we->cond.get());
            worklist.push_back(we->body.get());
            break;
        }
        case NodeKind::SetBang:
            worklist.push_back(
                static_cast<const SetBangExpr *>(e)->expr.get());
            break;
        case NodeKind::Begin: {
            auto *beg = static_cast<const BeginExpr *>(e);
            for (const auto &sub : beg->exprs) {
                worklist.push_back(sub.get());
            }
            break;
        }
        default:
            break;
        }
    }

    // Use Z3 to verify: assert "found_violation" and check
    Z3_solver solver = Z3_mk_solver(ctx);
    Z3_solver_inc_ref(ctx, solver);
    Z3_solver_assert(ctx, solver,
        found_violation ? Z3_mk_true(ctx) : Z3_mk_false(ctx));
    bool ok = Z3_solver_check(ctx, solver) == Z3_L_FALSE; // UNSAT means no violation
    Z3_solver_dec_ref(ctx, solver);
    Z3_del_context(ctx);

    return ok;
}

TEST(UncoverGetZ3, NoVarForMutable) {
    // let x = 0; begin { set! x 42; x }
    // After uncover_get, x references should be GetExpr, not VarExpr
    std::vector<std::unique_ptr<Expr>> bexprs;
    bexprs.push_back(std::make_unique<SetBangExpr>(
        "x", std::make_unique<IntExpr>(42)));
    bexprs.push_back(std::make_unique<VarExpr>("x"));
    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(0),
        std::make_unique<BeginExpr>(std::move(bexprs)));
    Program prog(std::move(body));
    auto result = uncover_get(prog);

    std::set<std::string> mvars = {"x"};
    EXPECT_TRUE(z3_no_var_for_mutable(*result, mvars));
}
