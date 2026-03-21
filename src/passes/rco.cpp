#include "rco.h"

#include "clone_leaf.h"

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mc {

namespace {

enum class Need { Atom, Expr };

struct EvalFrame { const Expr *expr; Need need; };
struct UnaryBuild { UnaryOp op; Need need; };
struct BinBuildLhs { BinaryOp op; const Expr *rhs; Need need; };
struct BinBuildRhs { BinaryOp op; Need need; };
struct IfBuildCond { const Expr *then_br; const Expr *else_br; Need need; };
struct IfBuildThen { const Expr *else_br; Need need; };
struct IfBuildElse { Need need; };
struct LetBuildInit { std::string var; const Expr *body; };
struct LetBuildBody { std::string var; };
struct WhileBuildCond { const Expr *body; Need need; };
struct WhileBuildBody { Need need; };
struct SetBangBuild { std::string var; Need need; };
struct BeginBuild { std::vector<const Expr *> remaining; size_t total; Need need; };
struct VectorRefBuild { int64_t index; Need need; };
struct VectorSetVecBuild { int64_t index; const Expr *val; Need need; };
struct VectorSetValBuild { int64_t index; Need need; };
struct VectorLengthBuild { Need need; };
struct ApplyBuild { size_t total; std::vector<const Expr *> remaining; Need need; };

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorRefBuild, VectorSetVecBuild,
                           VectorSetValBuild, VectorLengthBuild,
                           ApplyBuild>;

using Binding = std::pair<std::string, std::unique_ptr<Expr>>;

struct Result {
    std::unique_ptr<Expr> expr;
    std::vector<Binding> bindings;
};

/// @brief If need==Atom and expr non-atomic, bind to fresh tmp
/// @modifies res.bindings, res.expr, tmp_counter
void atomize(Result &res, Need need, int &tmp_counter) {
    if (need != Need::Atom) return;
    auto k = res.expr->kind();
    bool is_atom = (k == NodeKind::Int || k == NodeKind::Bool || k == NodeKind::Var);
    if (is_atom) return;
    std::string tmp = "tmp." + std::to_string(tmp_counter++);
    res.bindings.push_back({tmp, std::move(res.expr)});
    res.expr = std::make_unique<VarExpr>(tmp);
}

/// @brief Wrap expr in nested let-bindings (innermost last)
/// @requires expr != nullptr
/// @ensures result is let b[0] in let b[1] in ... expr
std::unique_ptr<Expr> wrap_bindings(std::unique_ptr<Expr> expr,
                                     std::vector<Binding> &bindings) {
    // invariant: expr wrapped with bindings[i+1..] applied
    // decreases: i
    for (int i = static_cast<int>(bindings.size()) - 1; i >= 0; --i) {
        expr = std::make_unique<LetExpr>(
            std::move(bindings[i].first), std::move(bindings[i].second),
            std::move(expr));
    }
    return expr;
}

/// @brief Evaluate leaf or push continuation frames for RCO
/// @requires ef.expr != nullptr
/// @modifies stack, results, tmp_counter
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<Result> &results, int &tmp_counter) {
    const Expr *e = ef.expr;

    if (auto leaf = clone_leaf(e)) {
        results.push_back({std::move(*leaf), {}});
        return;
    }
    switch (e->kind()) {
    case NodeKind::Var:
        results.push_back({std::make_unique<VarExpr>(
            expr_cast<VarExpr>(e)->name), {}});
        break;
    case NodeKind::Read: {
        Result res = {std::make_unique<ReadExpr>(), {}};
        atomize(res, ef.need, tmp_counter);
        results.push_back(std::move(res));
        break;
    }
    case NodeKind::Unary: {
        auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryBuild{ue->op, ef.need});
        stack.push_back(EvalFrame{ue->operand.get(), Need::Atom});
        break;
    }
    case NodeKind::Binary: {
        auto *bine = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{bine->op, bine->rhs.get(), ef.need});
        stack.push_back(EvalFrame{bine->lhs.get(), Need::Atom});
        break;
    }
    case NodeKind::If: {
        auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get(), ef.need});
        stack.push_back(EvalFrame{ife->cond.get(), Need::Expr});
        break;
    }
    case NodeKind::Let: {
        auto *le = expr_cast<LetExpr>(e);
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get(), Need::Expr});
        break;
    }
    case NodeKind::While: {
        auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileBuildCond{we->body.get(), ef.need});
        stack.push_back(EvalFrame{we->cond.get(), Need::Expr});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = expr_cast<SetBangExpr>(e);
        stack.push_back(SetBangBuild{se->var_name, ef.need});
        stack.push_back(EvalFrame{se->expr.get(), Need::Expr});
        break;
    }
    case NodeKind::Begin: {
        auto *beg = expr_cast<BeginExpr>(e);
        if (beg->exprs.empty()) {
            results.push_back({std::make_unique<BeginExpr>(
                std::vector<std::unique_ptr<Expr>>{}), {}});
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < beg->exprs.size(); ++i) {
                remaining.push_back(beg->exprs[i].get());
            }
            stack.push_back(BeginBuild{std::move(remaining),
                                        beg->exprs.size(), ef.need});
            stack.push_back(EvalFrame{beg->exprs[0].get(), Need::Expr});
        }
        break;
    }
    case NodeKind::Get:
        results.push_back({std::make_unique<GetExpr>(
            expr_cast<GetExpr>(e)->name), {}});
        break;
    case NodeKind::Allocate: {
        auto *ae = expr_cast<AllocateExpr>(e);
        Result res = {std::make_unique<AllocateExpr>(ae->len, ae->type), {}};
        atomize(res, ef.need, tmp_counter);
        results.push_back(std::move(res));
        break;
    }
    case NodeKind::Collect: {
        auto *ce = expr_cast<CollectExpr>(e);
        Result res = {std::make_unique<CollectExpr>(ce->bytes), {}};
        atomize(res, ef.need, tmp_counter);
        results.push_back(std::move(res));
        break;
    }
    case NodeKind::GlobalValue: {
        auto *gv = expr_cast<GlobalValueExpr>(e);
        Result res = {std::make_unique<GlobalValueExpr>(gv->name), {}};
        atomize(res, ef.need, tmp_counter);
        results.push_back(std::move(res));
        break;
    }
    case NodeKind::VectorRef: {
        auto *vr = expr_cast<VectorRefExpr>(e);
        stack.push_back(VectorRefBuild{vr->index, ef.need});
        stack.push_back(EvalFrame{vr->vec.get(), Need::Atom});
        break;
    }
    case NodeKind::VectorSet: {
        auto *vs = expr_cast<VectorSetExpr>(e);
        stack.push_back(VectorSetVecBuild{vs->index, vs->val.get(), ef.need});
        stack.push_back(EvalFrame{vs->vec.get(), Need::Atom});
        break;
    }
    case NodeKind::VectorLength: {
        auto *vl = expr_cast<VectorLengthExpr>(e);
        stack.push_back(VectorLengthBuild{ef.need});
        stack.push_back(EvalFrame{vl->vec.get(), Need::Atom});
        break;
    }
    case NodeKind::FunRef: {
        auto *fr = expr_cast<FunRefExpr>(e);
        Result res = {std::make_unique<FunRefExpr>(fr->name, fr->arity), {}};
        atomize(res, ef.need, tmp_counter);
        results.push_back(std::move(res));
        break;
    }
    case NodeKind::Apply: {
        auto *ap = expr_cast<ApplyExpr>(e);
        size_t total = 1 + ap->args.size();
        std::vector<const Expr *> remaining;
        for (size_t i = 0; i < ap->args.size(); ++i) {
            remaining.push_back(ap->args[i].get());
        }
        stack.push_back(ApplyBuild{total, std::move(remaining), ef.need});
        stack.push_back(EvalFrame{ap->func.get(), Need::Atom});
        break;
    }
    case NodeKind::Vector:
        // Should not appear after expose_allocation
        results.push_back({std::make_unique<VoidExpr>(), {}});
        break;
    }
}

/// @brief Process continuation frame, combining child results for RCO
/// @requires results has enough values for the continuation
/// @modifies stack, results, tmp_counter
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<Result> &results, int &tmp_counter) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back()); results.pop_back();
        Result res;
        res.bindings = std::move(operand.bindings);
        res.expr = std::make_unique<UnaryExpr>(ub->op, std::move(operand.expr));
        atomize(res, ub->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op, bl->need});
        stack.push_back(EvalFrame{bl->rhs, Need::Atom});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto rhs = std::move(results.back()); results.pop_back();
        auto lhs = std::move(results.back()); results.pop_back();
        Result res;
        res.bindings = std::move(lhs.bindings);
        for (auto &b : rhs.bindings) res.bindings.push_back(std::move(b));
        res.expr = std::make_unique<BinaryExpr>(br->op, std::move(lhs.expr),
                                                 std::move(rhs.expr));
        atomize(res, br->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br, ic->need});
        stack.push_back(EvalFrame{ic->then_br, Need::Expr});
    } else if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{it->need});
        stack.push_back(EvalFrame{it->else_br, Need::Expr});
    } else if (auto *ie = std::get_if<IfBuildElse>(&frame)) {
        auto else_r = std::move(results.back()); results.pop_back();
        auto then_r = std::move(results.back()); results.pop_back();
        auto cond_r = std::move(results.back()); results.pop_back();
        // Wrap branches with their bindings
        auto cond_e = wrap_bindings(std::move(cond_r.expr), cond_r.bindings);
        auto then_e = wrap_bindings(std::move(then_r.expr), then_r.bindings);
        auto else_e = wrap_bindings(std::move(else_r.expr), else_r.bindings);
        Result res;
        res.expr = std::make_unique<IfExpr>(
            std::move(cond_e), std::move(then_e), std::move(else_e));
        atomize(res, ie->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body, Need::Expr});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto init = std::move(results.back()); results.pop_back();
        auto init_w = wrap_bindings(std::move(init.expr), init.bindings);
        auto body_w = wrap_bindings(std::move(body.expr), body.bindings);
        Result res;
        res.expr = std::make_unique<LetExpr>(lb->var, std::move(init_w),
                                              std::move(body_w));
        results.push_back(std::move(res));
    } else if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{wc->need});
        stack.push_back(EvalFrame{wc->body, Need::Expr});
    } else if (auto *wb = std::get_if<WhileBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto cond = std::move(results.back()); results.pop_back();
        auto cond_w = wrap_bindings(std::move(cond.expr), cond.bindings);
        auto body_w = wrap_bindings(std::move(body.expr), body.bindings);
        Result res;
        res.expr = std::make_unique<WhileExpr>(
            std::move(cond_w), std::move(body_w));
        atomize(res, wb->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        auto expr = std::move(results.back()); results.pop_back();
        auto expr_w = wrap_bindings(std::move(expr.expr), expr.bindings);
        Result res;
        res.expr = std::make_unique<SetBangExpr>(sb->var, std::move(expr_w));
        atomize(res, sb->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *vr = std::get_if<VectorRefBuild>(&frame)) {
        auto vec = std::move(results.back()); results.pop_back();
        Result res;
        res.bindings = std::move(vec.bindings);
        res.expr = std::make_unique<VectorRefExpr>(
            std::move(vec.expr), vr->index);
        atomize(res, vr->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *vsv = std::get_if<VectorSetVecBuild>(&frame)) {
        stack.push_back(VectorSetValBuild{vsv->index, vsv->need});
        stack.push_back(EvalFrame{vsv->val, Need::Atom});
    } else if (auto *vs = std::get_if<VectorSetValBuild>(&frame)) {
        auto val = std::move(results.back()); results.pop_back();
        auto vec = std::move(results.back()); results.pop_back();
        Result res;
        res.bindings = std::move(vec.bindings);
        for (auto &b : val.bindings) res.bindings.push_back(std::move(b));
        res.expr = std::make_unique<VectorSetExpr>(
            std::move(vec.expr), vs->index, std::move(val.expr));
        atomize(res, vs->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *vl = std::get_if<VectorLengthBuild>(&frame)) {
        auto vec = std::move(results.back()); results.pop_back();
        Result res;
        res.bindings = std::move(vec.bindings);
        res.expr = std::make_unique<VectorLengthExpr>(std::move(vec.expr));
        atomize(res, vl->need, tmp_counter);
        results.push_back(std::move(res));
    } else if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        if (ab->remaining.empty()) {
            size_t num_args = ab->total - 1;
            std::vector<std::unique_ptr<Expr>> args;
            std::vector<Binding> all_bindings;
            for (size_t i = 0; i < num_args; ++i) {
                auto r = std::move(results.back()); results.pop_back();
                for (auto &b : r.bindings) all_bindings.push_back(std::move(b));
                args.push_back(std::move(r.expr));
            }
            std::reverse(args.begin(), args.end());
            auto func_r = std::move(results.back()); results.pop_back();
            Result res;
            res.bindings = std::move(func_r.bindings);
            for (auto &b : all_bindings) res.bindings.push_back(std::move(b));
            res.expr = std::make_unique<ApplyExpr>(
                std::move(func_r.expr), std::move(args));
            atomize(res, ab->need, tmp_counter);
            results.push_back(std::move(res));
        } else {
            const Expr *next = ab->remaining[0];
            std::vector<const Expr *> rest(ab->remaining.begin() + 1,
                                            ab->remaining.end());
            stack.push_back(ApplyBuild{ab->total, std::move(rest), ab->need});
            stack.push_back(EvalFrame{next, Need::Atom});
        }
    } else if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (bb->remaining.empty()) {
            std::vector<std::unique_ptr<Expr>> exprs;
            for (size_t i = 0; i < bb->total; ++i) {
                auto r = std::move(results.back()); results.pop_back();
                exprs.push_back(wrap_bindings(std::move(r.expr), r.bindings));
            }
            std::reverse(exprs.begin(), exprs.end());
            Result res;
            res.expr = std::make_unique<BeginExpr>(std::move(exprs));
            atomize(res, bb->need, tmp_counter);
            results.push_back(std::move(res));
        } else {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1,
                                            bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total, bb->need});
            stack.push_back(EvalFrame{next, Need::Expr});
        }
    }
}

/// @brief Process a single expression through RCO
/// @requires root != nullptr
std::unique_ptr<Expr> rco_expr(const Expr *root, int &tmp_counter) {
    std::vector<Frame> stack;
    std::vector<Result> results;
    stack.push_back(EvalFrame{root, Need::Expr});

    // decreases: stack.size()
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
    return wrap_bindings(std::move(res.expr), res.bindings);
}

} // namespace

/// @brief Remove complex operands: all operator args become atomic
/// @requires prog.body != nullptr
/// @ensures result has only atomic (Int/Bool/Var) operands in Unary/Binary
std::unique_ptr<Program> remove_complex_operands(const Program &prog) {
    int tmp_counter = 0;
    std::vector<DefNode> new_defs;
    // invariant: new_defs[0..i) processed
    for (const auto &def : prog.defs) {
        auto new_body = rco_expr(def.body.get(), tmp_counter);
        new_defs.push_back(DefNode{def.name, def.params, def.ret_type,
                                    std::move(new_body)});
    }
    auto new_body = rco_expr(prog.body.get(), tmp_counter);
    return std::make_unique<Program>(std::move(new_defs), std::move(new_body));
}

} // namespace mc
