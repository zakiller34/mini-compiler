#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "passes/limit_functions.h"

using namespace mc;

// A def with <= 6 params is left structurally unchanged (params preserved).
TEST(LimitFunctions, PassthroughWhenLeq6) {
    // fn foo(a: Int, b: Int): Int { a + b }; foo(1, 2)
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
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->defs.size(), 1u);
    // params unchanged: still (a, b), no packing tuple introduced
    EXPECT_EQ(result->defs[0].params.size(), 2u);
    EXPECT_EQ(result->defs[0].params[0].first, "a");
    EXPECT_EQ(result->defs[0].params[1].first, "b");
}

// Exactly six params is the boundary: still passthrough (no packing).
TEST(LimitFunctions, ExactlySixPassthrough) {
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
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->defs.size(), 1u);
    EXPECT_EQ(result->defs[0].params.size(), 6u);
    EXPECT_EQ(result->defs[0].params[5].first, "f");
}

// Seven params: keep first five, pack the remaining two into a tuple param,
// so the def ends up with exactly six params (5 kept + 1 tuple).
TEST(LimitFunctions, PacksMoreThanSix) {
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "g";
    for (const char *n : {"a", "b", "c", "d", "e", "f", "h"}) {
        d.params.push_back({n, int_type()});
    }
    d.ret_type = int_type();
    // body uses the 7th param 'h' -> must be rewritten to tuple-read
    d.body = std::make_unique<VarExpr>("h");
    defs.push_back(std::move(d));

    auto body = std::make_unique<IntExpr>(0);
    Program prog(std::move(defs), std::move(body));
    auto result = limit_functions(prog);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->defs.size(), 1u);
    // 5 register params + 1 packed tuple param == 6
    ASSERT_EQ(result->defs[0].params.size(), 6u);
    EXPECT_EQ(result->defs[0].params[4].first, "e");
    // the 6th param is the packing tuple, holding the 2 overflow params
    const auto &tup_type = result->defs[0].params[5].second;
    ASSERT_TRUE(is_vector_type(tup_type));
    EXPECT_EQ(tup_type->elem_types.size(), 2u);
    // body reference to 'h' became a VectorRef into the tuple param
    EXPECT_EQ(result->defs[0].body->kind(), NodeKind::VectorRef);
}

// A call site with > 6 args packs the overflow args into a tuple, so the
// resulting Apply has exactly six argument expressions.
TEST(LimitFunctions, PacksCallSiteArgs) {
    std::vector<DefNode> defs;
    std::vector<std::unique_ptr<Expr>> args;
    for (int i = 0; i < 7; ++i) {
        args.push_back(std::make_unique<IntExpr>(i));
    }
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<FunRefExpr>("g", 7), std::move(args));

    Program prog(std::move(defs), std::move(body));
    auto result = limit_functions(prog);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->body->kind(), NodeKind::Apply);
    const auto *ap = expr_cast<ApplyExpr>(result->body.get());
    ASSERT_EQ(ap->args.size(), 6u);
    // last arg is the packed tuple (a Vector of the 2 overflow args)
    EXPECT_EQ(ap->args[5]->kind(), NodeKind::Vector);
}
