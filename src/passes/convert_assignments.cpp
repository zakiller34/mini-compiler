#include "convert_assignments.h"

#include "any_rebuild.h"

#include "free_vars.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mc {

namespace {

using StrSet = std::set<std::string>;

/// @brief 1-tuple box holding e: vector(e).
std::unique_ptr<Expr> box_init(std::unique_ptr<Expr> e) {
    std::vector<std::unique_ptr<Expr>> v;
    v.push_back(std::move(e));
    return std::make_unique<VectorExpr>(std::move(v));
}

/// @brief Read through a box: name[0].
std::unique_ptr<Expr> box_read(const std::string &name) {
    return std::make_unique<VectorRefExpr>(std::make_unique<VarExpr>(name), 0);
}

/// @brief All set! target names anywhere under root.
StrSet collect_assigned(const Expr *root) {
    StrSet out;
    std::vector<const Expr *> work{root};
    // decreases: unvisited nodes; invariant: out has set! targets seen
    while (!work.empty()) {
        const Expr *e = work.back();
        work.pop_back();
        if (e->kind() == NodeKind::SetBang) {
            out.insert(expr_cast<SetBangExpr>(e)->var_name);
        }
        push_child_exprs(e, work);
    }
    return out;
}

/// @brief All vars captured (free) by any lambda under root.
StrSet collect_captured(const Expr *root) {
    StrSet out;
    std::vector<const Expr *> work{root};
    // decreases: unvisited nodes; invariant: out has captures of lambdas seen
    while (!work.empty()) {
        const Expr *e = work.back();
        work.pop_back();
        if (e->kind() == NodeKind::Lambda) {
            auto *la = expr_cast<LambdaExpr>(e);
            StrSet params;
            for (const auto &p : la->params) params.insert(p.first);
            for (const auto &v : free_vars(la->body.get())) {
                if (params.find(v) == params.end()) out.insert(v);
            }
        }
        push_child_exprs(e, work);
    }
    return out;
}

struct EvalFrame { const Expr *expr; };
struct UnaryBuild { UnaryOp op; };
struct BinBuildLhs { BinaryOp op; const Expr *rhs; };
struct BinBuildRhs { BinaryOp op; };
struct IfBuildCond { const Expr *then_br; const Expr *else_br; };
struct IfBuildThen { const Expr *else_br; };
struct IfBuildElse {};
struct LetBuildInit { std::string var; const Expr *body; bool box; };
struct LetBuildBody { std::string var; bool box; };
struct WhileBuildCond { const Expr *body; };
struct WhileBuildBody {};
struct SetBangBuild { std::string var; bool box; };
struct BeginBuild { std::vector<const Expr *> remaining; size_t total; };
struct VectorBuild { size_t total; std::vector<const Expr *> remaining; };
struct VectorRefBuild { int64_t index; };
struct VectorSetVecBuild { int64_t index; const Expr *val; };
struct VectorSetValBuild { int64_t index; };
struct VectorLengthBuild {};
struct ProcArityBuild {};
struct LambdaBuild {
    std::vector<std::pair<std::string, TypePtr>> params;
    TypePtr ret_type;
};
struct ApplyBuild { size_t total; };

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorBuild, VectorRefBuild,
                           VectorSetVecBuild, VectorSetValBuild,
                           VectorLengthBuild, ProcArityBuild,
                           LambdaBuild, ApplyBuild,
                           AnyBuildFrame>;

/// State threaded through the rewrite: the box set + fresh-name counter.
struct AsgState { const StrSet *box; int *counter; };

std::vector<std::unique_ptr<Expr>> pop_n(
    std::vector<std::unique_ptr<Expr>> &results, size_t n) {
    std::vector<std::unique_ptr<Expr>> out;
    // decreases: n-i; invariant: out has last i popped
    for (size_t i = 0; i < n; ++i) {
        out.push_back(std::move(results.back()));
        results.pop_back();
    }
    std::reverse(out.begin(), out.end());
    return out;
}

/// @brief Wrap body with `let p = vector(p_raw)` for each boxed param;
///        renames those params to fresh raw names in place.
/// @modifies params, counter
std::unique_ptr<Expr> box_params(
    std::unique_ptr<Expr> body,
    std::vector<std::pair<std::string, TypePtr>> &params, const AsgState &st) {
    // decreases: params.size()-i; invariant: params[0..i) boxed as needed
    for (auto &p : params) {
        if (st.box->find(p.first) == st.box->end()) continue;
        std::string raw = p.first + ".raw." + std::to_string((*st.counter)++);
        body = std::make_unique<LetExpr>(
            p.first, box_init(std::make_unique<VarExpr>(raw)), std::move(body));
        p.first = raw;
    }
    return body;
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void push_eval(const Expr *e, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results, AsgState &st) {
    if (push_any_eval<EvalFrame>(e, stack)) return;
    switch (e->kind()) {
    case NodeKind::Int:
        results.push_back(std::make_unique<IntExpr>(expr_cast<IntExpr>(e)->value));
        break;
    case NodeKind::Bool:
        results.push_back(std::make_unique<BoolExpr>(expr_cast<BoolExpr>(e)->value));
        break;
    case NodeKind::Read:
        results.push_back(std::make_unique<ReadExpr>());
        break;
    case NodeKind::Void:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    case NodeKind::FunRef: {
        auto *fr = expr_cast<FunRefExpr>(e);
        results.push_back(std::make_unique<FunRefExpr>(fr->name, fr->arity));
        break;
    }
    case NodeKind::Var: {
        auto *ve = expr_cast<VarExpr>(e);
        if (st.box->find(ve->name) != st.box->end()) results.push_back(box_read(ve->name));
        else results.push_back(std::make_unique<VarExpr>(ve->name));
        break;
    }
    case NodeKind::Get: {
        auto *ge = expr_cast<GetExpr>(e);
        if (st.box->find(ge->name) != st.box->end()) results.push_back(box_read(ge->name));
        else results.push_back(std::make_unique<GetExpr>(ge->name));
        break;
    }
    case NodeKind::Unary: {
        auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        auto *be = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{be->op, be->rhs.get()});
        stack.push_back(EvalFrame{be->lhs.get()});
        break;
    }
    case NodeKind::If: {
        auto *ie = expr_cast<IfExpr>(e);
        stack.push_back(IfBuildCond{ie->then_branch.get(), ie->else_branch.get()});
        stack.push_back(EvalFrame{ie->cond.get()});
        break;
    }
    case NodeKind::Let: {
        auto *le = expr_cast<LetExpr>(e);
        bool box = st.box->find(le->var) != st.box->end();
        stack.push_back(LetBuildInit{le->var, le->body.get(), box});
        stack.push_back(EvalFrame{le->init.get()});
        break;
    }
    case NodeKind::While: {
        auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileBuildCond{we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = expr_cast<SetBangExpr>(e);
        bool box = st.box->find(se->var_name) != st.box->end();
        stack.push_back(SetBangBuild{se->var_name, box});
        stack.push_back(EvalFrame{se->expr.get()});
        break;
    }
    case NodeKind::Begin: {
        auto *be = expr_cast<BeginExpr>(e);
        std::vector<const Expr *> rest;
        for (size_t i = 1; i < be->exprs.size(); ++i) rest.push_back(be->exprs[i].get());
        stack.push_back(BeginBuild{std::move(rest), be->exprs.size()});
        if (!be->exprs.empty()) stack.push_back(EvalFrame{be->exprs[0].get()});
        break;
    }
    case NodeKind::Vector: {
        auto *ve = expr_cast<VectorExpr>(e);
        std::vector<const Expr *> rest;
        for (size_t i = 1; i < ve->elems.size(); ++i) rest.push_back(ve->elems[i].get());
        stack.push_back(VectorBuild{ve->elems.size(), std::move(rest)});
        if (!ve->elems.empty()) stack.push_back(EvalFrame{ve->elems[0].get()});
        break;
    }
    case NodeKind::VectorRef: {
        auto *vr = expr_cast<VectorRefExpr>(e);
        stack.push_back(VectorRefBuild{vr->index});
        stack.push_back(EvalFrame{vr->vec.get()});
        break;
    }
    case NodeKind::VectorSet: {
        auto *vs = expr_cast<VectorSetExpr>(e);
        stack.push_back(VectorSetVecBuild{vs->index, vs->val.get()});
        stack.push_back(EvalFrame{vs->vec.get()});
        break;
    }
    case NodeKind::VectorLength: {
        auto *vl = expr_cast<VectorLengthExpr>(e);
        stack.push_back(VectorLengthBuild{});
        stack.push_back(EvalFrame{vl->vec.get()});
        break;
    }
    case NodeKind::ProcArity: {
        auto *pa = expr_cast<ProcArityExpr>(e);
        stack.push_back(ProcArityBuild{});
        stack.push_back(EvalFrame{pa->expr.get()});
        break;
    }
    case NodeKind::Lambda: {
        auto *la = expr_cast<LambdaExpr>(e);
        stack.push_back(LambdaBuild{la->params, la->ret_type});
        stack.push_back(EvalFrame{la->body.get()});
        break;
    }
    case NodeKind::Apply: {
        auto *ap = expr_cast<ApplyExpr>(e);
        stack.push_back(ApplyBuild{1 + ap->args.size()});
        for (size_t i = 0; i < ap->args.size(); ++i)
            stack.push_back(EvalFrame{ap->args[ap->args.size() - 1 - i].get()});
        stack.push_back(EvalFrame{ap->func.get()});
        break;
    }
    default:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    }
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results, AsgState &st) {
    if (auto *anyb = std::get_if<AnyBuildFrame>(&frame)) {
        build_any(*anyb, results);
    } else if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto o = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<UnaryExpr>(ub->op, std::move(o)));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto r = std::move(results.back()); results.pop_back();
        auto l = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<BinaryExpr>(br->op, std::move(l), std::move(r)));
    } else if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br});
        stack.push_back(EvalFrame{ic->then_br});
    } else if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{});
        stack.push_back(EvalFrame{it->else_br});
    } else if (std::get_if<IfBuildElse>(&frame) != nullptr) {
        auto e = std::move(results.back()); results.pop_back();
        auto t = std::move(results.back()); results.pop_back();
        auto c = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<IfExpr>(std::move(c), std::move(t), std::move(e)));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        stack.push_back(LetBuildBody{li->var, li->box});
        stack.push_back(EvalFrame{li->body});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto init = std::move(results.back()); results.pop_back();
        if (lb->box) init = box_init(std::move(init));
        results.push_back(std::make_unique<LetExpr>(lb->var, std::move(init), std::move(body)));
    } else if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{});
        stack.push_back(EvalFrame{wc->body});
    } else if (std::get_if<WhileBuildBody>(&frame) != nullptr) {
        auto body = std::move(results.back()); results.pop_back();
        auto cond = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<WhileExpr>(std::move(cond), std::move(body)));
    } else if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        auto ex = std::move(results.back()); results.pop_back();
        if (sb->box) {
            results.push_back(std::make_unique<VectorSetExpr>(
                std::make_unique<VarExpr>(sb->var), 0, std::move(ex)));
        } else {
            results.push_back(std::make_unique<SetBangExpr>(sb->var, std::move(ex)));
        }
    } else if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (bb->remaining.empty()) {
            results.push_back(std::make_unique<BeginExpr>(pop_n(results, bb->total)));
        } else {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1, bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vb = std::get_if<VectorBuild>(&frame)) {
        if (vb->remaining.empty()) {
            results.push_back(std::make_unique<VectorExpr>(pop_n(results, vb->total)));
        } else {
            const Expr *next = vb->remaining[0];
            std::vector<const Expr *> rest(vb->remaining.begin() + 1, vb->remaining.end());
            stack.push_back(VectorBuild{vb->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vr = std::get_if<VectorRefBuild>(&frame)) {
        auto v = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorRefExpr>(std::move(v), vr->index));
    } else if (auto *vsv = std::get_if<VectorSetVecBuild>(&frame)) {
        stack.push_back(VectorSetValBuild{vsv->index});
        stack.push_back(EvalFrame{vsv->val});
    } else if (auto *vs = std::get_if<VectorSetValBuild>(&frame)) {
        auto val = std::move(results.back()); results.pop_back();
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorSetExpr>(std::move(vec), vs->index, std::move(val)));
    } else if (std::get_if<VectorLengthBuild>(&frame) != nullptr) {
        auto v = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorLengthExpr>(std::move(v)));
    } else if (std::get_if<ProcArityBuild>(&frame) != nullptr) {
        auto v = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<ProcArityExpr>(std::move(v)));
    } else if (auto *lam = std::get_if<LambdaBuild>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto params = lam->params;
        body = box_params(std::move(body), params, st);
        results.push_back(std::make_unique<LambdaExpr>(
            std::move(params), lam->ret_type, std::move(body)));
    } else if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        auto all = pop_n(results, ab->total);
        auto func = std::move(all[0]);
        std::vector<std::unique_ptr<Expr>> args;
        for (size_t i = 1; i < all.size(); ++i) args.push_back(std::move(all[i]));
        results.push_back(std::make_unique<ApplyExpr>(std::move(func), std::move(args)));
    }
}

std::unique_ptr<Expr> rewrite(const Expr *root, const StrSet &box, int &counter) {
    AsgState st{&box, &counter};
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    stack.push_back(EvalFrame{root});
    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(ef->expr, stack, results, st);
        } else {
            process_cont(frame, stack, results, st);
        }
    }
    return std::move(results.back());
}

} // namespace

/// @brief Assignment-conversion driver: box assigned+captured variables.
std::unique_ptr<Program> convert_assignments(const Program &prog) {
    StrSet assigned;
    StrSet captured;
    // invariant: assigned/captured accumulate over defs[0..i) + body
    for (const auto &def : prog.defs) {
        for (const auto &n : collect_assigned(def.body.get())) assigned.insert(n);
        for (const auto &n : collect_captured(def.body.get())) captured.insert(n);
    }
    for (const auto &n : collect_assigned(prog.body.get())) assigned.insert(n);
    for (const auto &n : collect_captured(prog.body.get())) captured.insert(n);
    StrSet box;
    for (const auto &n : assigned) {
        if (captured.find(n) != captured.end()) box.insert(n);
    }

    int counter = 1;
    std::vector<DefNode> new_defs;
    // invariant: new_defs[0..i) are assignment-converted
    for (const auto &def : prog.defs) {
        auto body = rewrite(def.body.get(), box, counter);
        auto params = def.params;
        body = box_params(std::move(body), params, AsgState{&box, &counter});
        new_defs.push_back(DefNode{def.name, std::move(params), def.ret_type,
                                   std::move(body)});
    }
    auto main_body = rewrite(prog.body.get(), box, counter);
    return std::make_unique<Program>(std::move(new_defs), std::move(main_body));
}

} // namespace mc
