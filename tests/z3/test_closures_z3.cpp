#include <gtest/gtest.h>
#include <z3.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"
#include "passes/convert_to_closures.h"
#include "passes/free_vars.h"

using namespace mc;

namespace {

/// @brief Descend through Let bindings to find the first ClosureExpr.
/// @ensures result is a ClosureExpr* or nullptr
const ClosureExpr *find_closure(const Expr *e) {
    // decreases: Let-nesting depth; invariant: e is body-of-let chain
    while (e != nullptr && e->kind() == NodeKind::Let) {
        e = expr_cast<LetExpr>(e)->body.get();
    }
    if (e != nullptr && e->kind() == NodeKind::Closure) {
        return expr_cast<ClosureExpr>(e);
    }
    return nullptr;
}

/// @brief Names of captured free vars in a closure tuple (elems[1..]).
std::set<std::string> captured_names(const ClosureExpr *clos) {
    std::set<std::string> out;
    // invariant: out has VarExpr names from elems[1..i)
    for (size_t i = 1; i < clos->elems.size(); ++i) {
        if (clos->elems[i]->kind() == NodeKind::Var) {
            out.insert(expr_cast<VarExpr>(clos->elems[i].get())->name);
        }
    }
    return out;
}

/// @brief Z3-check that predicate `p` holds (assert p; expect SAT).
bool z3_holds(bool p) {
    Z3_config cfg = Z3_mk_config();
    Z3_context ctx = Z3_mk_context(cfg);
    Z3_del_config(cfg);
    Z3_solver solver = Z3_mk_solver(ctx);
    Z3_solver_inc_ref(ctx, solver);
    Z3_solver_assert(ctx, solver, p ? Z3_mk_true(ctx) : Z3_mk_false(ctx));
    bool sat = Z3_solver_check(ctx, solver) == Z3_L_TRUE;
    Z3_solver_dec_ref(ctx, solver);
    Z3_del_context(ctx);
    return sat;
}

/// @brief `let x = 5; let z = 7; lambda(y:Int):Int { y + x + z }`.
/// The lambda captures free vars {x, z}; y is a param (bound).
std::pair<Program, std::set<std::string>> program_with_capture() {
    std::vector<std::pair<std::string, TypePtr>> params;
    params.emplace_back("y", int_type());
    auto lam_body = std::make_unique<BinaryExpr>(
        BinaryOp::Add,
        std::make_unique<BinaryExpr>(BinaryOp::Add,
                                     std::make_unique<VarExpr>("y"),
                                     std::make_unique<VarExpr>("x")),
        std::make_unique<VarExpr>("z"));
    auto lam = std::make_unique<LambdaExpr>(std::move(params), int_type(),
                                            std::move(lam_body));
    std::set<std::string> expected = free_vars(lam.get()); // excludes param y

    auto body = std::make_unique<LetExpr>(
        "x", std::make_unique<IntExpr>(5),
        std::make_unique<LetExpr>("z", std::make_unique<IntExpr>(7),
                                  std::move(lam)));
    return {Program(std::move(body)), std::move(expected)};
}

} // namespace

// free_vars_subset_captured: every free var of the lambda body appears in the
// closure tuple.  ForAll fv in free_vars(body): fv in closure.
TEST(ClosuresZ3, FreeVarsSubsetCaptured) {
    auto [prog, expected] = program_with_capture();
    auto out = convert_to_closures(prog);
    const ClosureExpr *clos = find_closure(out->body.get());
    ASSERT_NE(clos, nullptr);
    auto captured = captured_names(clos);

    bool subset = true;
    for (const auto &fv : expected) {
        if (captured.count(fv) == 0U) subset = false;
    }
    EXPECT_TRUE(z3_holds(subset));
}

// closure_tuple_has_all_fvs: the closure captures EXACTLY the free vars — no
// missing captures, no spurious extras.
TEST(ClosuresZ3, ClosureTupleHasAllFvs) {
    auto [prog, expected] = program_with_capture();
    auto out = convert_to_closures(prog);
    const ClosureExpr *clos = find_closure(out->body.get());
    ASSERT_NE(clos, nullptr);
    auto captured = captured_names(clos);
    EXPECT_TRUE(z3_holds(captured == expected));
}
