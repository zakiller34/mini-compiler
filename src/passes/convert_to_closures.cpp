#include "convert_to_closures.h"

#include "free_vars.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mc {

namespace {

/// Shared mutable state threaded through the closure-conversion stack machine.
struct ClosState {
    std::vector<DefNode> *new_defs;
    int *counter;
};

/// @brief Placeholder type for a closure param / value (a heap tuple).
/// @ensures result is a non-empty Vector type (treated as a GC root)
TypePtr closure_type() { return vector_type({int_type()}); }

struct EvalFrame { const Expr *expr; };
struct UnaryBuild { UnaryOp op; };
struct BinBuildLhs { BinaryOp op; const Expr *rhs; };
struct BinBuildRhs { BinaryOp op; };
struct IfBuildCond { const Expr *then_br; const Expr *else_br; };
struct IfBuildThen { const Expr *else_br; };
struct IfBuildElse {};
struct LetBuildInit { std::string var; const Expr *body; };
struct LetBuildBody { std::string var; };
struct WhileBuildCond { const Expr *body; };
struct WhileBuildBody {};
struct SetBangBuild { std::string var; };
struct BeginBuild { std::vector<const Expr *> remaining; size_t total; };
struct VectorBuild { size_t total; std::vector<const Expr *> remaining; };
struct VectorRefBuild { int64_t index; };
struct VectorSetVecBuild { int64_t index; const Expr *val; };
struct VectorSetValBuild { int64_t index; };
struct VectorLengthBuild {};
struct ProcArityBuild {};
struct LambdaBuild {
    int64_t arity;
    std::vector<std::pair<std::string, TypePtr>> params;
    TypePtr ret_type;
    std::vector<std::string> fv;
    std::string clos_name;
    std::string lifted_name;
};
struct ApplyBuild { size_t total; };                    // general: func + args
struct DirectApplyBuild { std::string name; int64_t arity; size_t nargs; };

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorBuild, VectorRefBuild,
                           VectorSetVecBuild, VectorSetValBuild,
                           VectorLengthBuild, ProcArityBuild,
                           LambdaBuild, ApplyBuild, DirectApplyBuild>;

/// @brief Build closure tuple: [FunRef(name, arity+1), Var(fv)...].
std::unique_ptr<Expr> build_closure(int64_t arity, const std::string &name,
                                    const std::vector<std::string> &fv) {
    std::vector<std::unique_ptr<Expr>> elems;
    elems.push_back(std::make_unique<FunRefExpr>(name, arity + 1));
    // invariant: elems has code ptr + fv[0..i); decreases: fv.size()-i
    for (const auto &v : fv) {
        elems.push_back(std::make_unique<VarExpr>(v));
    }
    return std::make_unique<ClosureExpr>(arity, std::move(elems));
}

/// @brief Wrap a converted lambda body with `let fv_i = clos[i+1]` prelude.
std::unique_ptr<Expr> wrap_fv_lets(std::unique_ptr<Expr> body,
                                   const std::string &clos_name,
                                   const std::vector<std::string> &fv) {
    // decreases: i; invariant: body wrapped with fv[i+1..] lets
    for (int i = static_cast<int>(fv.size()) - 1; i >= 0; --i) {
        auto ref = std::make_unique<VectorRefExpr>(
            std::make_unique<VarExpr>(clos_name), i + 1);
        body = std::make_unique<LetExpr>(fv[static_cast<size_t>(i)],
                                         std::move(ref), std::move(body));
    }
    return body;
}

/// @brief Pop n converted exprs off results (restoring source order).
std::vector<std::unique_ptr<Expr>> pop_n(
    std::vector<std::unique_ptr<Expr>> &results, size_t n) {
    std::vector<std::unique_ptr<Expr>> out;
    // decreases: n-i; invariant: out has last i popped (reversed)
    for (size_t i = 0; i < n; ++i) {
        out.push_back(std::move(results.back()));
        results.pop_back();
    }
    std::reverse(out.begin(), out.end());
    return out;
}

/// @brief Evaluate leaf or push continuation frames for closure conversion.
/// @requires ef.expr != nullptr
void push_eval(const Expr *e, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results, ClosState &st) {
    switch (e->kind()) {
    case NodeKind::Int:
        results.push_back(std::make_unique<IntExpr>(expr_cast<IntExpr>(e)->value));
        break;
    case NodeKind::Bool:
        results.push_back(std::make_unique<BoolExpr>(expr_cast<BoolExpr>(e)->value));
        break;
    case NodeKind::Var:
        results.push_back(std::make_unique<VarExpr>(expr_cast<VarExpr>(e)->name));
        break;
    case NodeKind::Get:
        results.push_back(std::make_unique<GetExpr>(expr_cast<GetExpr>(e)->name));
        break;
    case NodeKind::Read:
        results.push_back(std::make_unique<ReadExpr>());
        break;
    case NodeKind::Void:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    case NodeKind::FunRef: {
        auto *fr = expr_cast<FunRefExpr>(e);
        // Function used as a first-class value: wrap in a closure (no fvs).
        results.push_back(build_closure(fr->arity, fr->name, {}));
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
        stack.push_back(LetBuildInit{le->var, le->body.get()});
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
        stack.push_back(SetBangBuild{se->var_name});
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
        auto fv = free_vars_sorted(la->body.get());
        // remove params from fv (they are bound by the lambda itself)
        std::vector<std::string> captured;
        for (const auto &v : fv) {
            bool is_param = false;
            for (const auto &p : la->params) if (p.first == v) is_param = true;
            if (!is_param) captured.push_back(v);
        }
        int n = (*st.counter)++;
        std::string lifted = "lambda." + std::to_string(n);
        std::string clos = "clos." + std::to_string(n);
        stack.push_back(LambdaBuild{static_cast<int64_t>(la->params.size()),
                                    la->params, la->ret_type, captured, clos,
                                    lifted});
        stack.push_back(EvalFrame{la->body.get()});
        break;
    }
    case NodeKind::Apply: {
        auto *ap = expr_cast<ApplyExpr>(e);
        if (ap->func->kind() == NodeKind::FunRef) {
            // Direct call to a known top-level function: no closure alloc.
            auto *fr = expr_cast<FunRefExpr>(ap->func.get());
            stack.push_back(DirectApplyBuild{fr->name, fr->arity, ap->args.size()});
            for (size_t i = 0; i < ap->args.size(); ++i)
                stack.push_back(EvalFrame{ap->args[ap->args.size() - 1 - i].get()});
        } else {
            stack.push_back(ApplyBuild{1 + ap->args.size()});
            for (size_t i = 0; i < ap->args.size(); ++i)
                stack.push_back(EvalFrame{ap->args[ap->args.size() - 1 - i].get()});
            stack.push_back(EvalFrame{ap->func.get()});
        }
        break;
    }
    default:
        results.push_back(std::make_unique<VoidExpr>()); // unreachable pre-conv
        break;
    }
}

/// @brief Rebuild a converted node from its children on the results stack.
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results, ClosState &st) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
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
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto init = std::move(results.back()); results.pop_back();
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
        results.push_back(std::make_unique<SetBangExpr>(sb->var, std::move(ex)));
    } else if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (bb->remaining.empty()) {
            auto exprs = pop_n(results, bb->total);
            results.push_back(std::make_unique<BeginExpr>(std::move(exprs)));
        } else {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1, bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vb = std::get_if<VectorBuild>(&frame)) {
        if (vb->remaining.empty()) {
            auto elems = pop_n(results, vb->total);
            results.push_back(std::make_unique<VectorExpr>(std::move(elems)));
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
        std::vector<std::pair<std::string, TypePtr>> params;
        params.emplace_back(lam->clos_name, closure_type());
        for (auto &p : lam->params) params.push_back(p);
        auto lifted_body = wrap_fv_lets(std::move(body), lam->clos_name, lam->fv);
        st.new_defs->push_back(DefNode{lam->lifted_name, std::move(params),
                                       lam->ret_type, std::move(lifted_body)});
        results.push_back(build_closure(lam->arity, lam->lifted_name, lam->fv));
    } else if (auto *da = std::get_if<DirectApplyBuild>(&frame)) {
        auto args = pop_n(results, da->nargs);
        std::vector<std::unique_ptr<Expr>> call_args;
        call_args.push_back(std::make_unique<IntExpr>(0)); // unused clos slot
        for (auto &a : args) call_args.push_back(std::move(a));
        results.push_back(std::make_unique<ApplyExpr>(
            std::make_unique<FunRefExpr>(da->name, da->arity + 1),
            std::move(call_args)));
    } else if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        auto all = pop_n(results, ab->total);
        auto func = std::move(all[0]);
        std::string c = "c." + std::to_string((*st.counter)++);
        std::vector<std::unique_ptr<Expr>> call_args;
        call_args.push_back(std::make_unique<VarExpr>(c));
        for (size_t i = 1; i < all.size(); ++i) call_args.push_back(std::move(all[i]));
        auto callee = std::make_unique<VectorRefExpr>(std::make_unique<VarExpr>(c), 0);
        auto app = std::make_unique<ApplyExpr>(std::move(callee), std::move(call_args));
        results.push_back(std::make_unique<LetExpr>(c, std::move(func), std::move(app)));
    }
}

/// @brief Run closure conversion on one expression.
std::unique_ptr<Expr> convert_expr(const Expr *root,
                                   std::vector<DefNode> &new_defs, int &counter) {
    ClosState st{&new_defs, &counter};
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

/// @brief Closure conversion driver: convert every def + main body, giving
///        each existing def a leading `clos` param, accumulating lifted defs.
std::unique_ptr<Program> convert_to_closures(const Program &prog) {
    int counter = 1;
    std::vector<DefNode> new_defs;
    // invariant: new_defs has converted defs[0..i) + their lifted lambdas
    for (const auto &def : prog.defs) {
        std::string clos = "clos.def." + std::to_string(counter++);
        auto body = convert_expr(def.body.get(), new_defs, counter);
        std::vector<std::pair<std::string, TypePtr>> params;
        params.emplace_back(clos, closure_type());
        for (const auto &p : def.params) params.push_back(p);
        new_defs.push_back(DefNode{def.name, std::move(params), def.ret_type,
                                   std::move(body)});
    }
    auto main_body = convert_expr(prog.body.get(), new_defs, counter);
    return std::make_unique<Program>(std::move(new_defs), std::move(main_body));
}

} // namespace mc
