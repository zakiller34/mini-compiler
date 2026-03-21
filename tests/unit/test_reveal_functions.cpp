#include <gtest/gtest.h>

#include <memory>

#include "ast.h"
#include "passes/reveal_functions.h"

using namespace mc;

/// @brief Check if any expr in tree is FunRefExpr with given name
/// @requires root != nullptr
/// @ensures returns true iff FunRef(name) found
static bool has_fun_ref(const Expr *root, const std::string &name) {
    // Iterative BFS using stack
    std::vector<const Expr *> stack;
    stack.push_back(root);
    // invariant: checked[0..i) did not contain FunRef(name)
    while (!stack.empty()) {
        const auto *e = stack.back();
        stack.pop_back();
        if (!e) continue;
        if (e->kind() == NodeKind::FunRef) {
            if (expr_cast<FunRefExpr>(e)->name == name) return true;
        }
        switch (e->kind()) {
        case NodeKind::Unary:
            stack.push_back(expr_cast<UnaryExpr>(e)->operand.get()); break;
        case NodeKind::Binary:
            stack.push_back(expr_cast<BinaryExpr>(e)->lhs.get());
            stack.push_back(expr_cast<BinaryExpr>(e)->rhs.get()); break;
        case NodeKind::If:
            stack.push_back(expr_cast<IfExpr>(e)->cond.get());
            stack.push_back(expr_cast<IfExpr>(e)->then_branch.get());
            stack.push_back(expr_cast<IfExpr>(e)->else_branch.get()); break;
        case NodeKind::Let:
            stack.push_back(expr_cast<LetExpr>(e)->init.get());
            stack.push_back(expr_cast<LetExpr>(e)->body.get()); break;
        case NodeKind::Apply:
            stack.push_back(expr_cast<ApplyExpr>(e)->func.get());
            for (const auto &a : expr_cast<ApplyExpr>(e)->args)
                stack.push_back(a.get());
            break;
        case NodeKind::Begin:
            for (const auto &x : expr_cast<BeginExpr>(e)->exprs)
                stack.push_back(x.get());
            break;
        default: break;
        }
    }
    return false;
}

/// @brief Check no VarExpr with given name exists in tree
static bool no_var_named(const Expr *root, const std::string &name) {
    std::vector<const Expr *> stack;
    stack.push_back(root);
    // invariant: checked nodes so far had no VarExpr(name)
    while (!stack.empty()) {
        const auto *e = stack.back();
        stack.pop_back();
        if (!e) continue;
        if (e->kind() == NodeKind::Var) {
            if (expr_cast<VarExpr>(e)->name == name) return false;
        }
        switch (e->kind()) {
        case NodeKind::Unary:
            stack.push_back(expr_cast<UnaryExpr>(e)->operand.get()); break;
        case NodeKind::Binary:
            stack.push_back(expr_cast<BinaryExpr>(e)->lhs.get());
            stack.push_back(expr_cast<BinaryExpr>(e)->rhs.get()); break;
        case NodeKind::If:
            stack.push_back(expr_cast<IfExpr>(e)->cond.get());
            stack.push_back(expr_cast<IfExpr>(e)->then_branch.get());
            stack.push_back(expr_cast<IfExpr>(e)->else_branch.get()); break;
        case NodeKind::Let:
            stack.push_back(expr_cast<LetExpr>(e)->init.get());
            stack.push_back(expr_cast<LetExpr>(e)->body.get()); break;
        case NodeKind::Apply:
            stack.push_back(expr_cast<ApplyExpr>(e)->func.get());
            for (const auto &a : expr_cast<ApplyExpr>(e)->args)
                stack.push_back(a.get());
            break;
        case NodeKind::Begin:
            for (const auto &x : expr_cast<BeginExpr>(e)->exprs)
                stack.push_back(x.get());
            break;
        default: break;
        }
    }
    return true;
}

TEST(RevealFunctions, SingleFnRef) {
    // fn foo(x: int): int { x + 1 }; foo(42)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<BinaryExpr>(
        BinaryOp::Add, std::make_unique<VarExpr>("x"),
        std::make_unique<IntExpr>(1));
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<IntExpr>(42));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("foo"), std::move(args));

    Program prog(std::move(defs), std::move(body));
    auto result = reveal_functions(prog);
    EXPECT_TRUE(has_fun_ref(result->body.get(), "foo"));
    EXPECT_TRUE(no_var_named(result->body.get(), "foo"));
}

TEST(RevealFunctions, NonFnVarUntouched) {
    // fn foo(x: int): int { x }; let y = 10; foo(y)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "foo";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(std::make_unique<VarExpr>("y"));
    auto body = std::make_unique<LetExpr>(
        "y", std::make_unique<IntExpr>(10),
        std::make_unique<ApplyExpr>(
            std::make_unique<VarExpr>("foo"), std::move(args)));

    Program prog(std::move(defs), std::move(body));
    auto result = reveal_functions(prog);
    // y should still be VarExpr, not FunRef
    EXPECT_FALSE(no_var_named(result->body.get(), "y"));
    EXPECT_TRUE(has_fun_ref(result->body.get(), "foo"));
}

TEST(RevealFunctions, FunRefHasCorrectArity) {
    // fn bar(a: int, b: int): int { a + b }; bar(1, 2)
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "bar";
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
        std::make_unique<VarExpr>("bar"), std::move(args));

    Program prog(std::move(defs), std::move(body));
    auto result = reveal_functions(prog);
    EXPECT_TRUE(has_fun_ref(result->body.get(), "bar"));
}

TEST(RevealFunctions, NestedApply) {
    // fn f(x: int): int { x }; f(f(1))
    std::vector<DefNode> defs;
    DefNode d;
    d.name = "f";
    d.params = {{"x", int_type()}};
    d.ret_type = int_type();
    d.body = std::make_unique<VarExpr>("x");
    defs.push_back(std::move(d));

    std::vector<std::unique_ptr<Expr>> inner_args;
    inner_args.push_back(std::make_unique<IntExpr>(1));
    auto inner = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("f"), std::move(inner_args));
    std::vector<std::unique_ptr<Expr>> outer_args;
    outer_args.push_back(std::move(inner));
    auto body = std::make_unique<ApplyExpr>(
        std::make_unique<VarExpr>("f"), std::move(outer_args));

    Program prog(std::move(defs), std::move(body));
    auto result = reveal_functions(prog);
    EXPECT_TRUE(has_fun_ref(result->body.get(), "f"));
    EXPECT_TRUE(no_var_named(result->body.get(), "f"));
}
