#include "uniquify.h"

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

using RenameEnv = std::map<std::string, std::string>;

struct EvalFrame { const Expr *expr; RenameEnv env; };
struct UnaryBuild { UnaryOp op; };
struct BinBuildLhs { BinaryOp op; const Expr *rhs; RenameEnv env; };
struct BinBuildRhs { BinaryOp op; };
struct IfBuildCond { const Expr *then_br; const Expr *else_br; RenameEnv env; };
struct IfBuildThen { const Expr *else_br; RenameEnv env; };
struct IfBuildElse {};
struct LetBuildInit { std::string old_var; std::string new_var; const Expr *body; RenameEnv env; };
struct LetBuildBody { std::string new_var; };
struct WhileBuildCond { const Expr *body; RenameEnv env; };
struct WhileBuildBody {};
struct SetBangBuild { std::string var_name; };
struct BeginBuild { std::vector<const Expr *> remaining; RenameEnv env; size_t total; };
struct VectorBuild { size_t total; std::vector<const Expr *> remaining; RenameEnv env; };
struct VectorRefBuild { int64_t index; };
struct VectorSetVecBuild { int64_t index; const Expr *val; RenameEnv env; };
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

/// @brief Evaluate leaf or push continuation frames for uniquify
/// @requires ef.expr != nullptr
/// @modifies stack, results, counter
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results, int &counter) {
    const Expr *e = ef.expr;
    const auto &env = ef.env;

    switch (e->kind()) {
    case NodeKind::Int:
        results.push_back(std::make_unique<IntExpr>(
            expr_cast<IntExpr>(e)->value));
        break;
    case NodeKind::Bool:
        results.push_back(std::make_unique<BoolExpr>(
            expr_cast<BoolExpr>(e)->value));
        break;
    case NodeKind::Var: {
        auto *ve = expr_cast<VarExpr>(e);
        auto it = env.find(ve->name);
        std::string name = (it != env.end()) ? it->second : ve->name;
        results.push_back(std::make_unique<VarExpr>(name));
        break;
    }
    case NodeKind::Read:
        results.push_back(std::make_unique<ReadExpr>());
        break;
    case NodeKind::Unary: {
        auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get(), env});
        break;
    }
    case NodeKind::Binary: {
        auto *bine = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{bine->op, bine->rhs.get(), env});
        stack.push_back(EvalFrame{bine->lhs.get(), env});
        break;
    }
    case NodeKind::If: {
        auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get(), env});
        stack.push_back(EvalFrame{ife->cond.get(), env});
        break;
    }
    case NodeKind::Let: {
        auto *le = expr_cast<LetExpr>(e);
        std::string new_name = le->var + "." + std::to_string(counter++);
        stack.push_back(LetBuildInit{le->var, new_name, le->body.get(), env});
        stack.push_back(EvalFrame{le->init.get(), env});
        break;
    }
    case NodeKind::While: {
        auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileBuildCond{we->body.get(), env});
        stack.push_back(EvalFrame{we->cond.get(), env});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = expr_cast<SetBangExpr>(e);
        auto it = env.find(se->var_name);
        std::string name = (it != env.end()) ? it->second : se->var_name;
        stack.push_back(SetBangBuild{name});
        stack.push_back(EvalFrame{se->expr.get(), env});
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
            stack.push_back(BeginBuild{std::move(remaining), env,
                                        beg->exprs.size()});
            stack.push_back(EvalFrame{beg->exprs[0].get(), env});
        }
        break;
    }
    case NodeKind::Void:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    case NodeKind::Get: {
        auto *ge = expr_cast<GetExpr>(e);
        auto it = env.find(ge->name);
        std::string name = (it != env.end()) ? it->second : ge->name;
        results.push_back(std::make_unique<GetExpr>(name));
        break;
    }
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
                                         std::move(remaining), env});
            stack.push_back(EvalFrame{ve->elems[0].get(), env});
        }
        break;
    }
    case NodeKind::VectorRef: {
        auto *vr = expr_cast<VectorRefExpr>(e);
        stack.push_back(VectorRefBuild{vr->index});
        stack.push_back(EvalFrame{vr->vec.get(), env});
        break;
    }
    case NodeKind::VectorSet: {
        auto *vs = expr_cast<VectorSetExpr>(e);
        stack.push_back(VectorSetVecBuild{vs->index, vs->val.get(), env});
        stack.push_back(EvalFrame{vs->vec.get(), env});
        break;
    }
    case NodeKind::VectorLength: {
        auto *vl = expr_cast<VectorLengthExpr>(e);
        stack.push_back(VectorLengthBuild{});
        stack.push_back(EvalFrame{vl->vec.get(), env});
        break;
    }
    case NodeKind::Allocate:
    case NodeKind::Collect:
    case NodeKind::GlobalValue:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    }
}

/// @brief Process continuation frame, combining uniquified children
/// @requires results has enough values for the continuation
/// @modifies stack, results
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<UnaryExpr>(ub->op, std::move(operand)));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs, bl->env});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto rhs = std::move(results.back()); results.pop_back();
        auto lhs = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<BinaryExpr>(br->op, std::move(lhs), std::move(rhs)));
    } else if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br, ic->env});
        stack.push_back(EvalFrame{ic->then_br, ic->env});
    } else if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{});
        stack.push_back(EvalFrame{it->else_br, it->env});
    } else if (std::get_if<IfBuildElse>(&frame) != nullptr) {
        auto else_r = std::move(results.back()); results.pop_back();
        auto then_r = std::move(results.back()); results.pop_back();
        auto cond_r = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<IfExpr>(
            std::move(cond_r), std::move(then_r), std::move(else_r)));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        RenameEnv new_env = li->env;
        new_env[li->old_var] = li->new_var;
        stack.push_back(LetBuildBody{li->new_var});
        stack.push_back(EvalFrame{li->body, std::move(new_env)});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto init = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<LetExpr>(lb->new_var, std::move(init), std::move(body)));
    } else if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{});
        stack.push_back(EvalFrame{wc->body, wc->env});
    } else if (std::get_if<WhileBuildBody>(&frame) != nullptr) {
        auto body = std::move(results.back()); results.pop_back();
        auto cond = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<WhileExpr>(
            std::move(cond), std::move(body)));
    } else if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        auto expr = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<SetBangExpr>(
            sb->var_name, std::move(expr)));
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
            stack.push_back(BeginBuild{std::move(rest), bb->env, bb->total});
            stack.push_back(EvalFrame{next, bb->env});
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
            stack.push_back(VectorBuild{vb->total, std::move(rest), vb->env});
            stack.push_back(EvalFrame{next, vb->env});
        }
    } else if (auto *vr = std::get_if<VectorRefBuild>(&frame)) {
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorRefExpr>(
            std::move(vec), vr->index));
    } else if (auto *vsv = std::get_if<VectorSetVecBuild>(&frame)) {
        stack.push_back(VectorSetValBuild{vsv->index});
        stack.push_back(EvalFrame{vsv->val, vsv->env});
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

/// @brief Rename all let-bound variables to unique names (var.N)
/// @requires prog.body != nullptr
/// @ensures all let-bound names in result are globally unique
std::unique_ptr<Program> uniquify(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    int counter = 1;
    stack.push_back(EvalFrame{prog.body.get(), {}});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results, counter);
        } else {
            process_cont(frame, stack, results);
        }
    }
    return std::make_unique<Program>(std::move(results.back()));
}
