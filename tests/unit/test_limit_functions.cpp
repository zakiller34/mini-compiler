#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "passes/limit_functions.h"

using namespace mc;

TEST(LimitFunctions, PassthroughWhenLeq6) {
    // fn foo(a: int, b: int): int { a + b }; foo(1, 2)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"a", int_type()}, {"b", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<BinaryExpr>(
        BinaryOp::Add, std::make_unique<VarExpr>("a"),
        std::make_unique<VarExpr>("b"));
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(1));
    args.push_back(std::make_unique<IntExpr>(2));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<FunRefExpr>("foo", 2), std::move(args));

    Program prog(std::move(defs), std::move(body));
    auto result = limit_functions(prog);
    // nullptr means "use original" — no change needed
    EXPECT_EQ(result, nullptr);
}

TEST(LimitFunctions, ExactlySixPassthrough) {
    // fn f(a,b,c,d,e,f): int; passthrough
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "f";
    for (const char *n : {"a", "b", "c", "d", "e", "f"}) {
        d.params.push_back({n, int_type()});
    }
    d.ret_type = int_type();
    d.body = std::make_unique<IntExpr>(0);
    defs.push_back(std::move(d));

    auto body = std::make_unique<IntExpr>(0);
    Program prog(std::move(defs), std::move(body));
    auto result = limit_functions(prog);
    EXPECT_EQ(result, nullptr);
}
