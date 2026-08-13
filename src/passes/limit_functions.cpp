#include "limit_functions.h"

#include "any_rebuild.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mc {

namespace {

constexpr size_t kMaxRegArgs = 6;

using PackMap = std::map<std::string, int64_t>; // packed param -> tuple index

/// @brief Read a packed param out of the tuple param: tup[idx].
std::unique_ptr<Expr> tuple_read(const std::string &tup, int64_t idx) {
    return std::make_unique<VectorRefExpr>(std::make_unique<VarExpr>(tup), idx);
}

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
struct VectorBuild { size_t total; };
struct VectorRefBuild { int64_t index; };
struct VectorSetVecBuild { int64_t index; const Expr *val; };
struct VectorSetValBuild { int64_t index; };
struct VectorLengthBuild {};
struct ProcArityBuild {};
struct ClosureBuild { size_t total; int64_t arity; };
struct ApplyBuild { size_t total; };

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorBuild, VectorRefBuild,
                           VectorSetVecBuild, VectorSetValBuild,
                           VectorLengthBuild, ProcArityBuild,
                           ClosureBuild, ApplyBuild,
                           AnyBuildFrame>;

struct LimState { const PackMap *pack; const std::string *tup; };

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

/// @brief If args exceed 6, pack args[5..] into a vector as the 6th arg.
std::vector<std::unique_ptr<Expr>> limit_args(
    std::vector<std::unique_ptr<Expr>> args) {
    if (args.size() <= kMaxRegArgs) return args;
    std::vector<std::unique_ptr<Expr>> out;
    // invariant: out has args[0..i)
    for (size_t i = 0; i < kMaxRegArgs - 1; ++i) out.push_back(std::move(args[i]));
    std::vector<std::unique_ptr<Expr>> packed;
    for (size_t i = kMaxRegArgs - 1; i < args.size(); ++i)
        packed.push_back(std::move(args[i]));
    out.push_back(std::make_unique<VectorExpr>(std::move(packed)));
    return out;
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void push_eval(const Expr *e, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results, LimState &st) {
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
        auto it = st.pack->find(ve->name);
        if (it != st.pack->end()) results.push_back(tuple_read(*st.tup, it->second));
        else results.push_back(std::make_unique<VarExpr>(ve->name));
        break;
    }
    case NodeKind::Get: {
        auto *ge = expr_cast<GetExpr>(e);
        auto it = st.pack->find(ge->name);
        if (it != st.pack->end()) results.push_back(tuple_read(*st.tup, it->second));
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
        stack.push_back(VectorBuild{ve->elems.size()});
        // decreases: i; invariant: elems[i+1..] queued
        for (int i = static_cast<int>(ve->elems.size()) - 1; i >= 0; --i)
            stack.push_back(EvalFrame{ve->elems[static_cast<size_t>(i)].get()});
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
    case NodeKind::Closure: {
        auto *cl = expr_cast<ClosureExpr>(e);
        stack.push_back(ClosureBuild{cl->elems.size(), cl->arity});
        // decreases: i; invariant: elems[i+1..] queued
        for (int i = static_cast<int>(cl->elems.size()) - 1; i >= 0; --i)
            stack.push_back(EvalFrame{cl->elems[static_cast<size_t>(i)].get()});
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
                  std::vector<std::unique_ptr<Expr>> &results, LimState &st) {
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
        auto it = st.pack->find(sb->var);
        if (it != st.pack->end()) {
            results.push_back(std::make_unique<VectorSetExpr>(
                std::make_unique<VarExpr>(*st.tup), it->second, std::move(ex)));
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
        results.push_back(std::make_unique<VectorExpr>(pop_n(results, vb->total)));
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
    } else if (auto *cb = std::get_if<ClosureBuild>(&frame)) {
        results.push_back(std::make_unique<ClosureExpr>(cb->arity, pop_n(results, cb->total)));
    } else if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        auto all = pop_n(results, ab->total);
        auto func = std::move(all[0]);
        std::vector<std::unique_ptr<Expr>> args;
        for (size_t i = 1; i < all.size(); ++i) args.push_back(std::move(all[i]));
        results.push_back(std::make_unique<ApplyExpr>(
            std::move(func), limit_args(std::move(args))));
    }
}

std::unique_ptr<Expr> rewrite(const Expr *root, const PackMap &pack,
                              const std::string &tup) {
    LimState st{&pack, &tup};
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

/// @brief Rewrite one def: if >6 params, pack params[5..] into a tuple param.
DefNode limit_def(const DefNode &def, int &counter) {
    if (def.params.size() <= kMaxRegArgs) {
        return DefNode{def.name, def.params, def.ret_type,
                       rewrite(def.body.get(), {}, "")};
    }
    std::string tup = "params." + std::to_string(counter++);
    PackMap pack;
    std::vector<std::pair<std::string, TypePtr>> new_params;
    std::vector<TypePtr> packed_types;
    // invariant: new_params has kept params; pack maps packed params
    for (size_t i = 0; i < def.params.size(); ++i) {
        if (i < kMaxRegArgs - 1) {
            new_params.push_back(def.params[i]);
        } else {
            pack[def.params[i].first] =
                static_cast<int64_t>(i - (kMaxRegArgs - 1));
            packed_types.push_back(def.params[i].second);
        }
    }
    new_params.emplace_back(tup, vector_type(std::move(packed_types)));
    return DefNode{def.name, std::move(new_params), def.ret_type,
                   rewrite(def.body.get(), pack, tup)};
}

} // namespace

/// @brief Limit every function to <=6 params; pack overflow into a tuple.
std::unique_ptr<Program> limit_functions(const Program &prog) {
    int counter = 0;
    std::vector<DefNode> new_defs;
    // invariant: new_defs[0..i) are arity-limited
    for (const auto &def : prog.defs) {
        new_defs.push_back(limit_def(def, counter));
    }
    auto new_body = rewrite(prog.body.get(), {}, "");
    return std::make_unique<Program>(std::move(new_defs), std::move(new_body));
}

} // namespace mc
