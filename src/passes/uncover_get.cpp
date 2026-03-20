#include "uncover_get.h"

#include "clone_leaf.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace mc {

namespace {

/// Phase 1: collect all mutable vars (targets of set!)
/// @ensures result contains all var names appearing as SetBangExpr targets
std::set<std::string> collect_mutable_vars(const Expr *root) {
    std::set<std::string> result;
    std::vector<const Expr *> worklist;
    worklist.push_back(root);

    // decreases: worklist.size() + unvisited nodes
    // invariant: result has set! targets from all visited nodes
    while (!worklist.empty()) {
        const Expr *e = worklist.back();
        worklist.pop_back();

        switch (e->kind()) {
        case NodeKind::SetBang: {
            auto *se = expr_cast<SetBangExpr>(e);
            result.insert(se->var_name);
            worklist.push_back(se->expr.get());
            break;
        }
        case NodeKind::Unary:
            worklist.push_back(
                expr_cast<UnaryExpr>(e)->operand.get());
            break;
        case NodeKind::Binary: {
            auto *be = expr_cast<BinaryExpr>(e);
            worklist.push_back(be->lhs.get());
            worklist.push_back(be->rhs.get());
            break;
        }
        case NodeKind::If: {
            auto *ife = expr_cast<IfExpr>(e);
            worklist.push_back(ife->cond.get());
            worklist.push_back(ife->then_branch.get());
            worklist.push_back(ife->else_branch.get());
            break;
        }
        case NodeKind::Let: {
            auto *le = expr_cast<LetExpr>(e);
            worklist.push_back(le->init.get());
            worklist.push_back(le->body.get());
            break;
        }
        case NodeKind::While: {
            auto *we = expr_cast<WhileExpr>(e);
            worklist.push_back(we->cond.get());
            worklist.push_back(we->body.get());
            break;
        }
        case NodeKind::Begin: {
            auto *beg = expr_cast<BeginExpr>(e);
            for (const auto &sub : beg->exprs) {
                worklist.push_back(sub.get());
            }
            break;
        }
        case NodeKind::Vector: {
            auto *ve = expr_cast<VectorExpr>(e);
            for (const auto &el : ve->elems) {
                worklist.push_back(el.get());
            }
            break;
        }
        case NodeKind::VectorRef:
            worklist.push_back(
                expr_cast<VectorRefExpr>(e)->vec.get());
            break;
        case NodeKind::VectorSet: {
            auto *vs = expr_cast<VectorSetExpr>(e);
            worklist.push_back(vs->vec.get());
            worklist.push_back(vs->val.get());
            break;
        }
        case NodeKind::VectorLength:
            worklist.push_back(
                expr_cast<VectorLengthExpr>(e)->vec.get());
            break;
        default:
            break; // Int, Bool, Var, Read, Void, Get, Allocate, etc.: no children
        }
    }
    return result;
}

/// Phase 2: replace VarExpr(v) → GetExpr(v) where v in mutable_vars

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

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorBuild, VectorRefBuild,
                           VectorSetVecBuild, VectorSetValBuild,
                           VectorLengthBuild>;

/// @brief Evaluate leaf or push continuation frames for uncover_get
/// @requires ef.expr != nullptr
/// @modifies stack, results
void push_eval(const EvalFrame &ef, const std::set<std::string> &mvars,
               std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results) {
    const Expr *e = ef.expr;
    if (auto leaf = clone_leaf(e)) {
        results.push_back(std::move(*leaf));
        return;
    }
    switch (e->kind()) {
    case NodeKind::Var: {
        auto *ve = expr_cast<VarExpr>(e);
        if (mvars.count(ve->name) != 0U) {
            results.push_back(std::make_unique<GetExpr>(ve->name));
        } else {
            results.push_back(std::make_unique<VarExpr>(ve->name));
        }
        break;
    }
    case NodeKind::Unary: {
        auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        auto *bine = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{bine->op, bine->rhs.get()});
        stack.push_back(EvalFrame{bine->lhs.get()});
        break;
    }
    case NodeKind::If: {
        auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
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
        auto *beg = expr_cast<BeginExpr>(e);
        if (beg->exprs.empty()) {
            results.push_back(std::make_unique<BeginExpr>(
                std::vector<std::unique_ptr<Expr>>{}));
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < beg->exprs.size(); ++i) {
                remaining.push_back(beg->exprs[i].get());
            }
            stack.push_back(BeginBuild{std::move(remaining),
                                        beg->exprs.size()});
            stack.push_back(EvalFrame{beg->exprs[0].get()});
        }
        break;
    }
    case NodeKind::Get:
        results.push_back(std::make_unique<GetExpr>(
            expr_cast<GetExpr>(e)->name));
        break;
    case NodeKind::Vector: {
        auto *ve = expr_cast<VectorExpr>(e);
        if (ve->elems.empty()) {
            results.push_back(std::make_unique<VectorExpr>(
                std::vector<std::unique_ptr<Expr>>{}));
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < ve->elems.size(); ++i) {
                remaining.push_back(ve->elems[i].get());
            }
            stack.push_back(VectorBuild{ve->elems.size(),
                                         std::move(remaining)});
            stack.push_back(EvalFrame{ve->elems[0].get()});
        }
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
    case NodeKind::Allocate:
    case NodeKind::Collect:
    case NodeKind::GlobalValue:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    }
}

/// @brief Process continuation frame, combining child results
/// @requires results has enough values for the continuation
/// @modifies stack, results
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back()); results.pop_back();
        results.push_back(
            std::make_unique<UnaryExpr>(ub->op, std::move(operand)));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto rhs = std::move(results.back()); results.pop_back();
        auto lhs = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<BinaryExpr>(
            br->op, std::move(lhs), std::move(rhs)));
    } else if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br});
        stack.push_back(EvalFrame{ic->then_br});
    } else if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{});
        stack.push_back(EvalFrame{it->else_br});
    } else if (std::get_if<IfBuildElse>(&frame) != nullptr) {
        auto else_r = std::move(results.back()); results.pop_back();
        auto then_r = std::move(results.back()); results.pop_back();
        auto cond_r = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<IfExpr>(
            std::move(cond_r), std::move(then_r), std::move(else_r)));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto init = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<LetExpr>(
            lb->var, std::move(init), std::move(body)));
    } else if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{});
        stack.push_back(EvalFrame{wc->body});
    } else if (std::get_if<WhileBuildBody>(&frame) != nullptr) {
        auto body = std::move(results.back()); results.pop_back();
        auto cond = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<WhileExpr>(
            std::move(cond), std::move(body)));
    } else if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        auto expr = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<SetBangExpr>(
            sb->var, std::move(expr)));
    } else if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (bb->remaining.empty()) {
            std::vector<std::unique_ptr<Expr>> exprs;
            for (size_t i = 0; i < bb->total; ++i) {
                exprs.push_back(std::move(results.back()));
                results.pop_back();
            }
            std::reverse(exprs.begin(), exprs.end());
            results.push_back(std::make_unique<BeginExpr>(std::move(exprs)));
        } else {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1,
                                            bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vb = std::get_if<VectorBuild>(&frame)) {
        if (vb->remaining.empty()) {
            std::vector<std::unique_ptr<Expr>> elems;
            for (size_t i = 0; i < vb->total; ++i) {
                elems.push_back(std::move(results.back()));
                results.pop_back();
            }
            std::reverse(elems.begin(), elems.end());
            results.push_back(std::make_unique<VectorExpr>(std::move(elems)));
        } else {
            const Expr *next = vb->remaining[0];
            std::vector<const Expr *> rest(vb->remaining.begin() + 1,
                                            vb->remaining.end());
            stack.push_back(VectorBuild{vb->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vr = std::get_if<VectorRefBuild>(&frame)) {
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorRefExpr>(
            std::move(vec), vr->index));
    } else if (auto *vsv = std::get_if<VectorSetVecBuild>(&frame)) {
        stack.push_back(VectorSetValBuild{vsv->index});
        stack.push_back(EvalFrame{vsv->val});
    } else if (auto *vs = std::get_if<VectorSetValBuild>(&frame)) {
        auto val = std::move(results.back()); results.pop_back();
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorSetExpr>(
            std::move(vec), vs->index, std::move(val)));
    } else if (std::get_if<VectorLengthBuild>(&frame) != nullptr) {
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorLengthExpr>(std::move(vec)));
    }
}

} // namespace

/// @brief Replace VarExpr with GetExpr for mutable variables (set! targets)
/// @requires prog.body != nullptr
/// @ensures result structurally identical except Var(v)->Get(v) for mutable v
std::unique_ptr<Program> uncover_get(const Program &prog) {
    auto mutable_vars = collect_mutable_vars(prog.body.get());

    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    stack.push_back(EvalFrame{prog.body.get()});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, mutable_vars, stack, results);
        } else {
            process_cont(frame, stack, results);
        }
    }
    return std::make_unique<Program>(std::move(results.back()));
}

} // namespace mc
