#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"
#include "passes/convert_to_closures.h"
#include "passes/free_vars.h"

using namespace mc;

namespace {

/// Count LambdaExpr nodes across a program's body and every def body.
int count_lambdas(const Program &prog) {
    int n = 0;
    std::vector<const Expr *> work;
    work.push_back(prog.body.get());
    for (const auto &d : prog.defs) work.push_back(d.body.get());
    // decreases: unvisited nodes; invariant: n counts lambdas visited
    while (!work.empty()) {
        const Expr *e = work.back();
        work.pop_back();
        if (e->kind() == NodeKind::Lambda) ++n;
        push_child_exprs(e, work);
    }
    return n;
}

/// `lambda(y:Int):Int { <body> }`.
std::unique_ptr<Expr> lam_y(std::unique_ptr<Expr> body) {
    std::vector<std::pair<std::string, TypePtr>> params;
    params.emplace_back("y", int_type());
    return std::make_unique<LambdaExpr>(std::move(params), int_type(),
                                        std::move(body));
}

} // namespace

// A closed lambda is lifted to a top-level def; the site becomes a Closure
// whose code pointer is a FunRef of arity+1 (extra leading clos param).
TEST(ClosureConversion, LiftsClosedLambda) {
    Program prog(lam_y(std::make_unique<VarExpr>("y")));
    auto out = convert_to_closures(prog);
    ASSERT_NE(out, nullptr);

    EXPECT_EQ(count_lambdas(*out), 0);          // no LambdaExpr remains
    ASSERT_EQ(out->defs.size(), 1u);            // one lifted def
    // lifted def gains a leading clos param: (clos, y)
    ASSERT_EQ(out->defs[0].params.size(), 2u);
    EXPECT_EQ(out->defs[0].params[0].first.rfind("clos", 0), 0u);

    ASSERT_EQ(out->body->kind(), NodeKind::Closure);
    const auto *clos = expr_cast<ClosureExpr>(out->body.get());
    ASSERT_EQ(clos->elems[0]->kind(), NodeKind::FunRef);
    EXPECT_EQ(expr_cast<FunRefExpr>(clos->elems[0].get())->arity, 2); // 1 + clos
}

// A lambda capturing a free var stores it in the closure tuple, and the lifted
// body reloads it via a `let fv = clos[i]` prelude.
TEST(ClosureConversion, CapturesFreeVar) {
    auto lam = lam_y(std::make_unique<BinaryExpr>(
        BinaryOp::Add, std::make_unique<VarExpr>("y"),
        std::make_unique<VarExpr>("x")));
    auto body = std::make_unique<LetExpr>("x", std::make_unique<IntExpr>(5),
                                          std::move(lam));
    Program prog(std::move(body));
    auto out = convert_to_closures(prog);

    EXPECT_EQ(count_lambdas(*out), 0);
    ASSERT_EQ(out->defs.size(), 1u);
    // lifted body reloads captured x from the closure -> starts with a Let
    EXPECT_EQ(out->defs[0].body->kind(), NodeKind::Let);

    // site: let x = 5; Closure[ FunRef, x ]  (2 elems: code ptr + captured x)
    ASSERT_EQ(out->body->kind(), NodeKind::Let);
    const auto *site = expr_cast<LetExpr>(out->body.get());
    ASSERT_EQ(site->body->kind(), NodeKind::Closure);
    EXPECT_EQ(expr_cast<ClosureExpr>(site->body.get())->elems.size(), 2u);
}

// Every existing top-level def gains a leading clos parameter.
TEST(ClosureConversion, DefGainsClosParam) {
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"a", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("a");
    defs.push_back(std::move(d));
    Program prog(std::move(defs), std::make_unique<IntExpr>(0));

    auto out = convert_to_closures(prog);
    ASSERT_EQ(out->defs.size(), 1u);
    EXPECT_EQ(out->defs[0].name, "foo");
    ASSERT_EQ(out->defs[0].params.size(), 2u); // clos + a
    EXPECT_EQ(out->defs[0].params[0].first.rfind("clos", 0), 0u);
    EXPECT_EQ(out->defs[0].params[1].first, "a");
}

// A bare FunRef used as a value is wrapped in a closure (arity+1 code ptr).
TEST(ClosureConversion, WrapsFunRefValue) {
    Program prog(std::make_unique<FunRefExpr>("foo", 1));
    auto out = convert_to_closures(prog);
    ASSERT_EQ(out->body->kind(), NodeKind::Closure);
    const auto *clos = expr_cast<ClosureExpr>(out->body.get());
    ASSERT_EQ(clos->elems[0]->kind(), NodeKind::FunRef);
    EXPECT_EQ(expr_cast<FunRefExpr>(clos->elems[0].get())->arity, 2);
}
