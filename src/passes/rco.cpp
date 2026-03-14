#include "rco.h"

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

/// Whether the result must be atomic (bound to a temp if complex)
enum class Need { Atom, Expr };

struct EvalFrame {
    const Expr *expr;
    Need need;
};

struct UnaryBuild {
    UnaryOp op;
    Need need;
};

struct BinBuildLhs {
    BinaryOp op;
    const Expr *rhs;
    Need need;
};

struct BinBuildRhs {
    BinaryOp op;
    Need need;
};

struct LetBuildInit {
    std::string var;
    const Expr *body;
};

struct LetBuildBody {
    std::string var;
};

using Frame =
    std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs, LetBuildInit, LetBuildBody>;

/// Pair of (bindings-to-wrap, result-expr)
using Binding = std::pair<std::string, std::unique_ptr<Expr>>;

struct Result {
    std::unique_ptr<Expr> expr;
    std::vector<Binding> bindings;
};

/// @brief Atomize: if need==Atom and expr is complex, bind to tmp
/// @requires res.expr != nullptr
/// @ensures if need==Atom, res.expr is IntExpr or VarExpr
void atomize(Result &res, Need need, int &tmp_counter) {
    if (need != Need::Atom) {
        return;
    }
    bool is_atom = (dynamic_cast<IntExpr *>(res.expr.get()) != nullptr) ||
                   (dynamic_cast<VarExpr *>(res.expr.get()) != nullptr);
    if (is_atom) {
        return;
    }
    std::string tmp = "tmp." + std::to_string(tmp_counter++);
    res.bindings.push_back({tmp, std::move(res.expr)});
    res.expr = std::make_unique<VarExpr>(tmp);
}

/// @brief Wrap an expression with let-bindings from a binding list
/// @requires expr != nullptr
/// @ensures result has all bindings applied as nested lets
std::unique_ptr<Expr> wrap_bindings(std::unique_ptr<Expr> expr, std::vector<Binding> &bindings) {
    // invariant: expr is wrapped with bindings[i+1..]
    // decreases: i
    for (int i = static_cast<int>(bindings.size()) - 1; i >= 0; --i) {
        expr = std::make_unique<LetExpr>(std::move(bindings[i].first),
                                         std::move(bindings[i].second), std::move(expr));
    }
    return expr;
}

/// @brief Handle eval frame for RCO pass
/// @requires ef.expr != nullptr
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack, std::vector<Result> &results,
               int &tmp_counter) {
    const Expr *e = ef.expr;

    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        results.push_back({std::make_unique<IntExpr>(ie->value), {}});
    } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        results.push_back({std::make_unique<VarExpr>(ve->name), {}});
    } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        Result res = {std::make_unique<ReadExpr>(), {}};
        atomize(res, ef.need, tmp_counter);
        results.push_back(std::move(res));
    } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        stack.push_back(UnaryBuild{ue->op, ef.need});
        stack.push_back(EvalFrame{ue->operand.get(), Need::Atom});
    } else if (const auto *be = dynamic_cast<const BinaryExpr *>(e)) {
        stack.push_back(BinBuildLhs{be->op, be->rhs.get(), ef.need});
        stack.push_back(EvalFrame{be->lhs.get(), Need::Atom});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get(), Need::Expr});
    }
}

/// @brief Process continuation frame for RCO
/// @requires results has enough elements
void process_cont(Frame &frame, std::vector<Frame> &stack, std::vector<Result> &results,
                  int &tmp_counter) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back());
        results.pop_back();
        Result res;
        res.bindings = std::move(operand.bindings);
        res.expr = std::make_unique<UnaryExpr>(ub->op, std::move(operand.expr));
        atomize(res, ub->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op, bl->need});
        stack.push_back(EvalFrame{bl->rhs, Need::Atom});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto rhs = std::move(results.back());
        results.pop_back();
        auto lhs = std::move(results.back());
        results.pop_back();
        Result res;
        res.bindings = std::move(lhs.bindings);
        // invariant: rhs.bindings appended after lhs.bindings
        for (auto &b : rhs.bindings) {
            res.bindings.push_back(std::move(b));
        }
        res.expr = std::make_unique<BinaryExpr>(br->op, std::move(lhs.expr), std::move(rhs.expr));
        atomize(res, br->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body, Need::Expr});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back());
        results.pop_back();
        auto init = std::move(results.back());
        results.pop_back();
        auto init_wrapped = wrap_bindings(std::move(init.expr), init.bindings);
        auto body_wrapped = wrap_bindings(std::move(body.expr), body.bindings);
        Result res;
        res.expr = std::make_unique<LetExpr>(lb->var, std::move(init_wrapped),
                                             std::move(body_wrapped));
        results.push_back(std::move(res));
    }
}

} // namespace

/// @brief Remove complex operands from program
/// @requires prog.body != nullptr
/// @ensures all +/- operands are atoms
std::unique_ptr<Program> remove_complex_operands(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<Result> results;
    int tmp_counter = 0;

    stack.push_back(EvalFrame{prog.body.get(), Need::Expr});

    // decreases stack.size()
    // invariant results holds RCO-transformed subtrees with pending bindings
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();

        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results, tmp_counter);
        } else {
            process_cont(frame, stack, results, tmp_counter);
        }
    }
    auto &res = results.back();
    auto body = wrap_bindings(std::move(res.expr), res.bindings);
    return std::make_unique<Program>(std::move(body));
}
