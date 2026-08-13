#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ast.h"
#include "passes/expose_allocation.h"
#include "passes/shrink.h"
#include "passes/uncover_get.h"
#include "passes/uniquify.h"

using namespace mc;

namespace {

/// Run the passes that precede expose_allocation, then the pass itself.
std::unique_ptr<Program> run_expose(std::unique_ptr<Expr> body) {
    Program prog(std::move(body));
    auto s = shrink(prog);
    auto u = uniquify(*s);
    auto g = uncover_get(*u);
    return expose_allocation(*g);
}

std::unique_ptr<VectorExpr> make_vector(std::vector<std::unique_ptr<Expr>> es) {
    return std::make_unique<VectorExpr>(std::move(es));
}

/// Count occurrences of a substring in the AST dump.
int count(const std::string &haystack, const std::string &needle) {
    int n = 0;
    for (size_t p = haystack.find(needle); p != std::string::npos;
         p = haystack.find(needle, p + needle.size())) {
        ++n;
    }
    return n;
}

} // namespace

TEST(ExposeAllocation, NoVectorExprSurvives) {
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    elems.push_back(std::make_unique<IntExpr>(2));
    auto out = run_expose(make_vector(std::move(elems)));
    auto dump = out->body->dump();
    EXPECT_EQ(count(dump, "vector "), 0) << dump;
    EXPECT_GT(count(dump, "allocate"), 0) << dump;
}

TEST(ExposeAllocation, EmitsGcCheckBeforeAllocate) {
    // The lowering must test free_ptr against fromspace_end and call collect
    // before allocating, or the bump allocator can run off the end of the heap.
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    auto out = run_expose(make_vector(std::move(elems)));
    auto dump = out->body->dump();
    EXPECT_GT(count(dump, "free_ptr"), 0) << dump;
    EXPECT_GT(count(dump, "fromspace_end"), 0) << dump;
    EXPECT_GT(count(dump, "collect"), 0) << dump;
    // The check has to come before the allocation, not after it.
    EXPECT_LT(dump.find("collect"), dump.find("allocate")) << dump;
}

TEST(ExposeAllocation, InitialisesEveryElement) {
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(10));
    elems.push_back(std::make_unique<IntExpr>(20));
    elems.push_back(std::make_unique<IntExpr>(30));
    auto out = run_expose(make_vector(std::move(elems)));
    auto dump = out->body->dump();
    // One vector-set! per element.
    EXPECT_EQ(count(dump, "vector-set!"), 3) << dump;
}

TEST(ExposeAllocation, AllocateCarriesTheElementCount) {
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<IntExpr>(1));
    elems.push_back(std::make_unique<IntExpr>(2));
    auto out = run_expose(make_vector(std::move(elems)));
    auto dump = out->body->dump();
    EXPECT_NE(dump.find("allocate 2"), std::string::npos) << dump;
}

TEST(ExposeAllocation, NestedVectorsAreBothLowered) {
    std::vector<std::unique_ptr<Expr>> inner;
    inner.push_back(std::make_unique<IntExpr>(1));
    std::vector<std::unique_ptr<Expr>> outer;
    outer.push_back(make_vector(std::move(inner)));
    outer.push_back(std::make_unique<IntExpr>(2));
    auto out = run_expose(make_vector(std::move(outer)));
    auto dump = out->body->dump();
    EXPECT_EQ(count(dump, "vector "), 0) << dump;
    EXPECT_EQ(count(dump, "allocate"), 2) << dump;
}

TEST(ExposeAllocation, VariableElementsAreThreaded) {
    // Regression (Phase 8 changeset 011): `vector(a, b)` with *variable*
    // elements aborted here, because the pass had no type environment for
    // let-bound names. Building a tuple out of variables is the common case.
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<VarExpr>("a"));
    elems.push_back(std::make_unique<VarExpr>("b"));
    auto body = std::make_unique<LetExpr>(
        "a", std::make_unique<IntExpr>(1),
        std::make_unique<LetExpr>("b", std::make_unique<IntExpr>(2),
                                  make_vector(std::move(elems))));
    auto out = run_expose(std::move(body));
    auto dump = out->body->dump();
    EXPECT_EQ(count(dump, "vector "), 0) << dump;
    EXPECT_EQ(count(dump, "vector-set!"), 2) << dump;
}

TEST(ExposeAllocation, LeavesNonVectorProgramsAlone) {
    auto body = std::make_unique<BinaryExpr>(BinaryOp::Add,
                                             std::make_unique<IntExpr>(1),
                                             std::make_unique<IntExpr>(2));
    auto out = run_expose(std::move(body));
    auto dump = out->body->dump();
    EXPECT_EQ(count(dump, "allocate"), 0) << dump;
    EXPECT_EQ(count(dump, "collect"), 0) << dump;
}
