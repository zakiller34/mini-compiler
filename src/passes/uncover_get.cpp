#include "uncover_get.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

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
            auto *se = static_cast<const SetBangExpr *>(e);
            result.insert(se->var_name);
            worklist.push_back(se->expr.get());
            break;
        }
        case NodeKind::Unary:
            worklist.push_back(
                static_cast<const UnaryExpr *>(e)->operand.get());
            break;
        case NodeKind::Binary: {
            auto *be = static_cast<const BinaryExpr *>(e);
            worklist.push_back(be->lhs.get());
            worklist.push_back(be->rhs.get());
            break;
        }
        case NodeKind::If: {
            auto *ife = static_cast<const IfExpr *>(e);
            worklist.push_back(ife->cond.get());
            worklist.push_back(ife->then_branch.get());
            worklist.push_back(ife->else_branch.get());
            break;
        }
        case NodeKind::Let: {
            auto *le = static_cast<const LetExpr *>(e);
            worklist.push_back(le->init.get());
            worklist.push_back(le->body.get());
            break;
        }
        case NodeKind::While: {
            auto *we = static_cast<const WhileExpr *>(e);
            worklist.push_back(we->cond.get());
            worklist.push_back(we->body.get());
            break;
        }
        case NodeKind::Begin: {
            auto *beg = static_cast<const BeginExpr *>(e);
            for (const auto &sub : beg->exprs) {
                worklist.push_back(sub.get());
            }
            break;
        }
        default:
            break; // Int, Bool, Var, Read, Void, Get: no children
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

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild>;

/// @brief Evaluate leaf or push continuation frames for uncover_get
/// @requires ef.expr != nullptr
/// @modifies stack, results
void push_eval(const EvalFrame &ef, const std::set<std::string> &mvars,
               std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results) {
    const Expr *e = ef.expr;
    switch (e->kind()) {
    case NodeKind::Int:
        results.push_back(std::make_unique<IntExpr>(
            static_cast<const IntExpr *>(e)->value));
        break;
    case NodeKind::Bool:
        results.push_back(std::make_unique<BoolExpr>(
            static_cast<const BoolExpr *>(e)->value));
        break;
    case NodeKind::Var: {
        auto *ve = static_cast<const VarExpr *>(e);
        if (mvars.count(ve->name) != 0U) {
            results.push_back(std::make_unique<GetExpr>(ve->name));
        } else {
            results.push_back(std::make_unique<VarExpr>(ve->name));
        }
        break;
    }
    case NodeKind::Read:
        results.push_back(std::make_unique<ReadExpr>());
        break;
    case NodeKind::Unary: {
        auto *ue = static_cast<const UnaryExpr *>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        auto *bine = static_cast<const BinaryExpr *>(e);
        stack.push_back(BinBuildLhs{bine->op, bine->rhs.get()});
        stack.push_back(EvalFrame{bine->lhs.get()});
        break;
    }
    case NodeKind::If: {
        auto *ife = static_cast<const IfExpr *>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
        break;
    }
    case NodeKind::Let: {
        auto *le = static_cast<const LetExpr *>(e);
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
        break;
    }
    case NodeKind::While: {
        auto *we = static_cast<const WhileExpr *>(e);
        stack.push_back(WhileBuildCond{we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = static_cast<const SetBangExpr *>(e);
        stack.push_back(SetBangBuild{se->var_name});
        stack.push_back(EvalFrame{se->expr.get()});
        break;
    }
    case NodeKind::Begin: {
        auto *beg = static_cast<const BeginExpr *>(e);
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
    case NodeKind::Void:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    case NodeKind::Get:
        results.push_back(std::make_unique<GetExpr>(
            static_cast<const GetExpr *>(e)->name));
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
