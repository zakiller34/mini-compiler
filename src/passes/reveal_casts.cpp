#include "reveal_casts.h"

#include "clone_leaf.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mc {

namespace {

using Results = std::vector<std::unique_ptr<Expr>>;

std::unique_ptr<Expr> pop(Results &results) {
    auto r = std::move(results.back());
    results.pop_back();
    return r;
}

std::unique_ptr<Expr> var(const std::string &name) {
    return std::make_unique<VarExpr>(name);
}

/// @brief (== lhs rhs) over Int operands
std::unique_ptr<Expr> int_eq(std::unique_ptr<Expr> lhs, int64_t rhs) {
    return std::make_unique<BinaryExpr>(BinaryOp::Eq, std::move(lhs),
                                        std::make_unique<IntExpr>(rhs));
}

/// @brief Tag code tested by a runtime type predicate
int64_t pred_tag(TypePred p) {
    switch (p) {
    case TypePred::Integer: return kTagInt;
    case TypePred::Boolean: return kTagBool;
    case TypePred::Vector: return kTagVector;
    case TypePred::Procedure: return kTagFunction;
    case TypePred::Void: return kTagVoid;
    }
    return kTagInt;
}

/// @brief Extra runtime check a projection needs beyond the tag
/// @requires is_flat_type(t)
/// @ensures nullptr for scalar targets; a length/arity test otherwise
std::unique_ptr<Expr> shape_check(const std::string &tmp, const TypePtr &t) {
    auto n = static_cast<int64_t>(t->elem_types.size());
    if (is_vector_type(t)) {
        return int_eq(std::make_unique<AnyVectorLengthExpr>(var(tmp)), n);
    }
    if (is_fun_type(t)) {
        // elem_types is [params..., ret], so the arity is one less
        return int_eq(std::make_unique<ProcArityExpr>(
                          std::make_unique<ValueOfExpr>(var(tmp), t)),
                      n - 1);
    }
    return nullptr;
}

/// @brief (Project e T) => let tmp = e; if tag ok then ValueOf else Exit
/// @requires is_flat_type(t)
/// @ensures the result never yields a value of the wrong tag
std::unique_ptr<Expr> lower_project(std::unique_ptr<Expr> src,
                                    const TypePtr &t, int64_t &counter) {
    std::string tmp = "proj." + std::to_string(counter++);
    std::unique_ptr<Expr> ok = std::make_unique<ValueOfExpr>(var(tmp), t);
    if (auto shape = shape_check(tmp, t)) {
        ok = std::make_unique<IfExpr>(std::move(shape), std::move(ok),
                                       std::make_unique<ExitExpr>());
    }
    auto guard = int_eq(std::make_unique<TagOfAnyExpr>(var(tmp)), tagof(t));
    auto body = std::make_unique<IfExpr>(std::move(guard), std::move(ok),
                                          std::make_unique<ExitExpr>());
    return std::make_unique<LetExpr>(tmp, std::move(src), std::move(body));
}

/// @brief Guard an any-vector access with a tag and a bounds check (9.5)
/// @requires prim uses only `vec_tmp`/`idx_tmp`, never re-evaluating them
std::unique_ptr<Expr> guard_any_vector(std::unique_ptr<Expr> vec,
                                       std::unique_ptr<Expr> idx,
                                       std::unique_ptr<Expr> prim,
                                       int64_t &counter,
                                       const std::string &vec_tmp,
                                       const std::string &idx_tmp) {
    (void)counter;
    auto in_bounds = std::make_unique<BinaryExpr>(
        BinaryOp::Lt, var(idx_tmp),
        std::make_unique<AnyVectorLengthExpr>(var(vec_tmp)));
    auto checked = std::make_unique<IfExpr>(
        std::move(in_bounds), std::move(prim),
        std::make_unique<ExitExpr>());
    auto is_vec = int_eq(std::make_unique<TagOfAnyExpr>(var(vec_tmp)),
                          kTagVector);
    auto body = std::make_unique<IfExpr>(
        std::move(is_vec), std::move(checked),
        std::make_unique<ExitExpr>());
    auto inner = std::make_unique<LetExpr>(idx_tmp, std::move(idx),
                                            std::move(body));
    return std::make_unique<LetExpr>(vec_tmp, std::move(vec),
                                      std::move(inner));
}

// -- Frame machine --

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
struct ApplyBuild { size_t total; std::vector<const Expr *> remaining; };
struct LambdaBuild {
    std::vector<std::pair<std::string, TypePtr>> params;
    TypePtr ret_type;
};
struct ProcArityBuild {};
struct InjectBuild { TypePtr ftype; };
struct ProjectBuild { TypePtr ftype; };
struct TypePredBuild { TypePred pred; };
struct AnyRefVecBuild { const Expr *idx; };
struct AnyRefIdxBuild {};
struct AnySetVecBuild { const Expr *idx; const Expr *val; };
struct AnySetIdxBuild { const Expr *val; };
struct AnySetValBuild {};
struct AnyLengthBuild {};
struct MakeAnyBuild { int64_t tag; };
struct TagOfAnyBuild {};
struct ValueOfBuild { TypePtr ftype; };

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild, VectorBuild,
                           VectorRefBuild, VectorSetVecBuild,
                           VectorSetValBuild, VectorLengthBuild,
                           ApplyBuild, LambdaBuild, ProcArityBuild,
                           InjectBuild, ProjectBuild, TypePredBuild,
                           AnyRefVecBuild, AnyRefIdxBuild,
                           AnySetVecBuild, AnySetIdxBuild, AnySetValBuild,
                           AnyLengthBuild, MakeAnyBuild, TagOfAnyBuild,
                           ValueOfBuild>;

/// @brief Push frames for the L_Any node kinds
/// @ensures returns false if e is not an L_Any node
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool push_eval_any(const Expr *e, std::vector<Frame> &stack,
                   Results &results) {
    switch (e->kind()) {
    case NodeKind::Inject: {
        const auto *ie = expr_cast<InjectExpr>(e);
        stack.push_back(InjectBuild{ie->ftype});
        stack.push_back(EvalFrame{ie->expr.get()});
        return true;
    }
    case NodeKind::Project: {
        const auto *pe = expr_cast<ProjectExpr>(e);
        stack.push_back(ProjectBuild{pe->ftype});
        stack.push_back(EvalFrame{pe->expr.get()});
        return true;
    }
    case NodeKind::TypePredicate: {
        const auto *tp = expr_cast<TypePredExpr>(e);
        stack.push_back(TypePredBuild{tp->pred});
        stack.push_back(EvalFrame{tp->expr.get()});
        return true;
    }
    case NodeKind::AnyVectorRef: {
        const auto *ar = expr_cast<AnyVectorRefExpr>(e);
        stack.push_back(AnyRefVecBuild{ar->idx.get()});
        stack.push_back(EvalFrame{ar->vec.get()});
        return true;
    }
    case NodeKind::AnyVectorSet: {
        const auto *as = expr_cast<AnyVectorSetExpr>(e);
        stack.push_back(AnySetVecBuild{as->idx.get(), as->val.get()});
        stack.push_back(EvalFrame{as->vec.get()});
        return true;
    }
    case NodeKind::AnyVectorLength:
        stack.push_back(AnyLengthBuild{});
        stack.push_back(
            EvalFrame{expr_cast<AnyVectorLengthExpr>(e)->vec.get()});
        return true;
    case NodeKind::MakeAny: {
        const auto *ma = expr_cast<MakeAnyExpr>(e);
        stack.push_back(MakeAnyBuild{ma->tag});
        stack.push_back(EvalFrame{ma->expr.get()});
        return true;
    }
    case NodeKind::TagOfAny:
        stack.push_back(TagOfAnyBuild{});
        stack.push_back(EvalFrame{expr_cast<TagOfAnyExpr>(e)->expr.get()});
        return true;
    case NodeKind::ValueOf: {
        const auto *vo = expr_cast<ValueOfExpr>(e);
        stack.push_back(ValueOfBuild{vo->ftype});
        stack.push_back(EvalFrame{vo->expr.get()});
        return true;
    }
    case NodeKind::Exit:
        results.push_back(std::make_unique<ExitExpr>());
        return true;
    default:
        return false;
    }
}

/// @brief Push frames for the tuple and function node kinds
/// @ensures returns false if e is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool push_eval_data(const Expr *e, std::vector<Frame> &stack) {
    switch (e->kind()) {
    case NodeKind::Vector: {
        const auto *ve = expr_cast<VectorExpr>(e);
        std::vector<const Expr *> rest;
        // invariant: rest has elems[1..i)
        for (size_t i = 1; i < ve->elems.size(); ++i) {
            rest.push_back(ve->elems[i].get());
        }
        stack.push_back(VectorBuild{ve->elems.size(), std::move(rest)});
        stack.push_back(EvalFrame{ve->elems[0].get()});
        return true;
    }
    case NodeKind::VectorRef: {
        const auto *vr = expr_cast<VectorRefExpr>(e);
        stack.push_back(VectorRefBuild{vr->index});
        stack.push_back(EvalFrame{vr->vec.get()});
        return true;
    }
    case NodeKind::VectorSet: {
        const auto *vs = expr_cast<VectorSetExpr>(e);
        stack.push_back(VectorSetVecBuild{vs->index, vs->val.get()});
        stack.push_back(EvalFrame{vs->vec.get()});
        return true;
    }
    case NodeKind::VectorLength:
        stack.push_back(VectorLengthBuild{});
        stack.push_back(EvalFrame{expr_cast<VectorLengthExpr>(e)->vec.get()});
        return true;
    case NodeKind::Apply: {
        const auto *ae = expr_cast<ApplyExpr>(e);
        std::vector<const Expr *> rest;
        // invariant: rest has args[0..i)
        for (const auto &a : ae->args) rest.push_back(a.get());
        stack.push_back(ApplyBuild{ae->args.size(), std::move(rest)});
        stack.push_back(EvalFrame{ae->func.get()});
        return true;
    }
    case NodeKind::Lambda: {
        const auto *la = expr_cast<LambdaExpr>(e);
        stack.push_back(LambdaBuild{la->params, la->ret_type});
        stack.push_back(EvalFrame{la->body.get()});
        return true;
    }
    case NodeKind::ProcArity:
        stack.push_back(ProcArityBuild{});
        stack.push_back(EvalFrame{expr_cast<ProcArityExpr>(e)->expr.get()});
        return true;
    default:
        return false;
    }
}

/// @brief Evaluate leaf or push continuation frames (enum-switch FSM)
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               Results &results) {
    const Expr *e = ef.expr;
    if (auto leaf = clone_leaf(e)) {
        results.push_back(std::move(*leaf));
        return;
    }
    if (push_eval_any(e, stack, results)) return;
    if (push_eval_data(e, stack)) return;

    switch (e->kind()) {
    case NodeKind::Var:
        results.push_back(var(expr_cast<VarExpr>(e)->name));
        break;
    case NodeKind::Get:
        results.push_back(
            std::make_unique<GetExpr>(expr_cast<GetExpr>(e)->name));
        break;
    case NodeKind::FunRef: {
        const auto *fr = expr_cast<FunRefExpr>(e);
        results.push_back(std::make_unique<FunRefExpr>(fr->name, fr->arity));
        break;
    }
    case NodeKind::Unary: {
        const auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        const auto *be = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{be->op, be->rhs.get()});
        stack.push_back(EvalFrame{be->lhs.get()});
        break;
    }
    case NodeKind::If: {
        const auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                    ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
        break;
    }
    case NodeKind::Let: {
        const auto *le = expr_cast<LetExpr>(e);
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
        break;
    }
    case NodeKind::While: {
        const auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileBuildCond{we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
        break;
    }
    case NodeKind::SetBang: {
        const auto *se = expr_cast<SetBangExpr>(e);
        stack.push_back(SetBangBuild{se->var_name});
        stack.push_back(EvalFrame{se->expr.get()});
        break;
    }
    case NodeKind::Begin: {
        const auto *beg = expr_cast<BeginExpr>(e);
        std::vector<const Expr *> rest;
        // invariant: rest has exprs[1..i)
        for (size_t i = 1; i < beg->exprs.size(); ++i) {
            rest.push_back(beg->exprs[i].get());
        }
        stack.push_back(BeginBuild{std::move(rest), beg->exprs.size()});
        stack.push_back(EvalFrame{beg->exprs[0].get()});
        break;
    }
    default:
        throw std::runtime_error("reveal_casts: unexpected node kind");
    }
}

/// @brief Build results for the cast-related frames
/// @ensures returns false if frame is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool build_casts(Frame &frame, Results &results, int64_t &counter) {
    if (auto *ib = std::get_if<InjectBuild>(&frame)) {
        results.push_back(std::make_unique<MakeAnyExpr>(
            pop(results), tagof(ib->ftype)));
        return true;
    }
    if (auto *pb = std::get_if<ProjectBuild>(&frame)) {
        results.push_back(lower_project(pop(results), pb->ftype, counter));
        return true;
    }
    if (auto *tb = std::get_if<TypePredBuild>(&frame)) {
        results.push_back(int_eq(
            std::make_unique<TagOfAnyExpr>(pop(results)),
            pred_tag(tb->pred)));
        return true;
    }
    if (auto *mb = std::get_if<MakeAnyBuild>(&frame)) {
        results.push_back(
            std::make_unique<MakeAnyExpr>(pop(results), mb->tag));
        return true;
    }
    if (std::get_if<TagOfAnyBuild>(&frame) != nullptr) {
        results.push_back(std::make_unique<TagOfAnyExpr>(pop(results)));
        return true;
    }
    if (auto *vb = std::get_if<ValueOfBuild>(&frame)) {
        results.push_back(
            std::make_unique<ValueOfExpr>(pop(results), vb->ftype));
        return true;
    }
    return false;
}

/// @brief Build results for the any-vector-* frames, adding runtime checks
/// @ensures returns false if frame is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool build_any_vector(Frame &frame, std::vector<Frame> &stack,
                      Results &results, int64_t &counter) {
    if (auto *rv = std::get_if<AnyRefVecBuild>(&frame)) {
        stack.push_back(AnyRefIdxBuild{});
        stack.push_back(EvalFrame{rv->idx});
        return true;
    }
    if (std::get_if<AnyRefIdxBuild>(&frame) != nullptr) {
        std::string vt = "avec." + std::to_string(counter++);
        std::string it = "aidx." + std::to_string(counter++);
        auto idx = pop(results);
        auto vec = pop(results);
        auto prim = std::make_unique<AnyVectorRefExpr>(var(vt), var(it));
        results.push_back(guard_any_vector(std::move(vec), std::move(idx),
                                            std::move(prim), counter, vt, it));
        return true;
    }
    if (auto *sv = std::get_if<AnySetVecBuild>(&frame)) {
        stack.push_back(AnySetIdxBuild{sv->val});
        stack.push_back(EvalFrame{sv->idx});
        return true;
    }
    if (auto *si = std::get_if<AnySetIdxBuild>(&frame)) {
        stack.push_back(AnySetValBuild{});
        stack.push_back(EvalFrame{si->val});
        return true;
    }
    if (std::get_if<AnySetValBuild>(&frame) != nullptr) {
        std::string vt = "avec." + std::to_string(counter++);
        std::string it = "aidx." + std::to_string(counter++);
        auto val = pop(results);
        auto idx = pop(results);
        auto vec = pop(results);
        auto prim = std::make_unique<AnyVectorSetExpr>(var(vt), var(it),
                                                        std::move(val));
        results.push_back(guard_any_vector(std::move(vec), std::move(idx),
                                            std::move(prim), counter, vt, it));
        return true;
    }
    if (std::get_if<AnyLengthBuild>(&frame) != nullptr) {
        results.push_back(
            std::make_unique<AnyVectorLengthExpr>(pop(results)));
        return true;
    }
    return false;
}

/// @brief Build results for the control-flow frames
/// @ensures returns false if frame is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool build_control(Frame &frame, std::vector<Frame> &stack,
                   Results &results) {
    if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br});
        stack.push_back(EvalFrame{ic->then_br});
        return true;
    }
    if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{});
        stack.push_back(EvalFrame{it->else_br});
        return true;
    }
    if (std::get_if<IfBuildElse>(&frame) != nullptr) {
        auto else_br = pop(results);
        auto then_br = pop(results);
        auto cond = pop(results);
        results.push_back(std::make_unique<IfExpr>(
            std::move(cond), std::move(then_br), std::move(else_br)));
        return true;
    }
    if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body});
        return true;
    }
    if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = pop(results);
        auto init = pop(results);
        results.push_back(std::make_unique<LetExpr>(
            lb->var, std::move(init), std::move(body)));
        return true;
    }
    if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{});
        stack.push_back(EvalFrame{wc->body});
        return true;
    }
    if (std::get_if<WhileBuildBody>(&frame) != nullptr) {
        auto body = pop(results);
        auto cond = pop(results);
        results.push_back(std::make_unique<WhileExpr>(std::move(cond),
                                                       std::move(body)));
        return true;
    }
    if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        results.push_back(
            std::make_unique<SetBangExpr>(sb->var, pop(results)));
        return true;
    }
    if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (!bb->remaining.empty()) {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1,
                                            bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total});
            stack.push_back(EvalFrame{next});
            return true;
        }
        Results exprs;
        // invariant: exprs has the last i results, reversed
        for (size_t i = 0; i < bb->total; ++i) exprs.push_back(pop(results));
        std::reverse(exprs.begin(), exprs.end());
        results.push_back(std::make_unique<BeginExpr>(std::move(exprs)));
        return true;
    }
    return false;
}

/// @brief Build results for the tuple and function frames
/// @ensures returns false if frame is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool build_data(Frame &frame, std::vector<Frame> &stack, Results &results) {
    if (auto *vb = std::get_if<VectorBuild>(&frame)) {
        if (!vb->remaining.empty()) {
            const Expr *next = vb->remaining[0];
            std::vector<const Expr *> rest(vb->remaining.begin() + 1,
                                            vb->remaining.end());
            stack.push_back(VectorBuild{vb->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
            return true;
        }
        Results elems;
        // invariant: elems has the last i results, reversed
        for (size_t i = 0; i < vb->total; ++i) elems.push_back(pop(results));
        std::reverse(elems.begin(), elems.end());
        results.push_back(std::make_unique<VectorExpr>(std::move(elems)));
        return true;
    }
    if (auto *vr = std::get_if<VectorRefBuild>(&frame)) {
        results.push_back(
            std::make_unique<VectorRefExpr>(pop(results), vr->index));
        return true;
    }
    if (auto *vsv = std::get_if<VectorSetVecBuild>(&frame)) {
        stack.push_back(VectorSetValBuild{vsv->index});
        stack.push_back(EvalFrame{vsv->val});
        return true;
    }
    if (auto *vs = std::get_if<VectorSetValBuild>(&frame)) {
        auto val = pop(results);
        auto vec = pop(results);
        results.push_back(std::make_unique<VectorSetExpr>(
            std::move(vec), vs->index, std::move(val)));
        return true;
    }
    if (std::get_if<VectorLengthBuild>(&frame) != nullptr) {
        results.push_back(
            std::make_unique<VectorLengthExpr>(pop(results)));
        return true;
    }
    if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        if (!ab->remaining.empty()) {
            const Expr *next = ab->remaining[0];
            std::vector<const Expr *> rest(ab->remaining.begin() + 1,
                                            ab->remaining.end());
            stack.push_back(ApplyBuild{ab->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
            return true;
        }
        Results args;
        // invariant: args has the last i results, reversed
        for (size_t i = 0; i < ab->total; ++i) args.push_back(pop(results));
        std::reverse(args.begin(), args.end());
        auto func = pop(results);
        results.push_back(std::make_unique<ApplyExpr>(std::move(func),
                                                       std::move(args)));
        return true;
    }
    if (auto *lam = std::get_if<LambdaBuild>(&frame)) {
        results.push_back(std::make_unique<LambdaExpr>(
            std::move(lam->params), std::move(lam->ret_type), pop(results)));
        return true;
    }
    if (std::get_if<ProcArityBuild>(&frame) != nullptr) {
        results.push_back(std::make_unique<ProcArityExpr>(pop(results)));
        return true;
    }
    return false;
}

/// @brief Process one continuation frame
void process_cont(Frame &frame, std::vector<Frame> &stack, Results &results,
                  int64_t &counter) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        results.push_back(
            std::make_unique<UnaryExpr>(ub->op, pop(results)));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto rhs = pop(results);
        auto lhs = pop(results);
        results.push_back(std::make_unique<BinaryExpr>(
            br->op, std::move(lhs), std::move(rhs)));
    } else if (!build_casts(frame, results, counter) &&
               !build_any_vector(frame, stack, results, counter) &&
               !build_control(frame, stack, results) &&
               !build_data(frame, stack, results)) {
        throw std::runtime_error("reveal_casts: unexpected continuation");
    }
}

/// @brief Reveal casts in one expression tree
/// @requires root != nullptr
std::unique_ptr<Expr> transform(const Expr *root, int64_t &counter) {
    std::vector<Frame> stack;
    Results results;
    stack.push_back(EvalFrame{root});

    // decreases: stack.size()
    // invariant: results holds the completed subtrees, in evaluation order
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results);
        } else {
            process_cont(frame, stack, results, counter);
        }
    }
    return pop(results);
}

} // namespace

std::unique_ptr<Program> reveal_casts(const Program &prog) {
    int64_t counter = 0;
    std::vector<DefNode> new_defs;
    // invariant: new_defs has defs[0..i) revealed
    for (const auto &def : prog.defs) {
        new_defs.push_back(DefNode{def.name, def.params, def.ret_type,
                                    transform(def.body.get(), counter)});
    }
    return std::make_unique<Program>(std::move(new_defs),
                                      transform(prog.body.get(), counter));
}

} // namespace mc
