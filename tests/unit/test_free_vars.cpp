#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ast.h"
#include "passes/free_vars.h"

using namespace mc;

// A bare variable is free.
TEST(FreeVars, BareVar) {
    auto e = std::make_unique<VarExpr>("x");
    auto fv = free_vars(e.get());
    EXPECT_EQ(fv, (std::set<std::string>{"x"}));
}

// let binds its variable: `let x = 1; x + y`  ->  free = {y}.
TEST(FreeVars, LetBoundExcluded) {
    auto body = std::make_unique<BinaryExpr>(
        BinaryOp::Add, std::make_unique<VarExpr>("x"),
        std::make_unique<VarExpr>("y"));
    auto e = std::make_unique<LetExpr>("x", std::make_unique<IntExpr>(1),
                                       std::move(body));
    EXPECT_EQ(free_vars(e.get()), (std::set<std::string>{"y"}));
}

// lambda params are bound: `lambda(x:Int):Int { x + y }`  ->  free = {y}.
TEST(FreeVars, LambdaParamExcluded) {
    std::vector<std::pair<std::string, TypePtr>> params;
    params.emplace_back("x", int_type());
    auto e = std::make_unique<LambdaExpr>(
        std::move(params), int_type(),
        std::make_unique<BinaryExpr>(BinaryOp::Add,
                                     std::make_unique<VarExpr>("x"),
                                     std::make_unique<VarExpr>("y")));
    EXPECT_EQ(free_vars(e.get()), (std::set<std::string>{"y"}));
}

// Nested lambdas: outer(x) { inner(y) { x + y + z } }  ->  free = {z}.
TEST(FreeVars, NestedLambdaCapturesOuter) {
    std::vector<std::pair<std::string, TypePtr>> inner_p;
    inner_p.emplace_back("y", int_type());
    auto inner_body = std::make_unique<BinaryExpr>(
        BinaryOp::Add,
        std::make_unique<BinaryExpr>(BinaryOp::Add,
                                     std::make_unique<VarExpr>("x"),
                                     std::make_unique<VarExpr>("y")),
        std::make_unique<VarExpr>("z"));
    auto inner = std::make_unique<LambdaExpr>(std::move(inner_p), int_type(),
                                              std::move(inner_body));
    std::vector<std::pair<std::string, TypePtr>> outer_p;
    outer_p.emplace_back("x", int_type());
    auto outer = std::make_unique<LambdaExpr>(std::move(outer_p), int_type(),
                                              std::move(inner));
    EXPECT_EQ(free_vars(outer.get()), (std::set<std::string>{"z"}));
}

// set! target and get() reads count as uses.
TEST(FreeVars, SetBangAndGetCounted) {
    auto e = std::make_unique<SetBangExpr>("x", std::make_unique<GetExpr>("y"));
    EXPECT_EQ(free_vars(e.get()), (std::set<std::string>{"x", "y"}));
}

// free_vars_sorted returns names in deterministic ascending order.
TEST(FreeVars, SortedDeterministic) {
    auto e = std::make_unique<BinaryExpr>(
        BinaryOp::Add,
        std::make_unique<BinaryExpr>(BinaryOp::Add,
                                     std::make_unique<VarExpr>("c"),
                                     std::make_unique<VarExpr>("a")),
        std::make_unique<VarExpr>("b"));
    EXPECT_EQ(free_vars_sorted(e.get()),
              (std::vector<std::string>{"a", "b", "c"}));
}
