#include "reveal_functions.h"

#include "clone_leaf.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mc {

namespace {

/// Map of function name -> arity
using FunMap = std::map<std::string, int64_t>;

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

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorBuild, VectorRefBuild,
                           VectorSetVecBuild, VectorSetValBuild,
                           VectorLengthBuild, ApplyBuild>;

/// @brief Evaluate leaf or push continuation frames
void push_eval(const EvalFrame &ef, const FunMap &funs,
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
        auto it = funs.find(ve->name);
        if (it != funs.end()) {
            results.push_back(
                std::make_unique<FunRefExpr>(ve->name, it->second));
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
        auto *be = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{be->op, be->rhs.get()});
        stack.push_back(EvalFrame{be->lhs.get()});
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
    case NodeKind::Apply: {
        auto *ae = expr_cast<ApplyExpr>(e);
        std::vector<const Expr *> remaining;
        for (size_t i = 0; i < ae->args.size(); ++i) {
            remaining.push_back(ae->args[i].get());
        }
        stack.push_back(ApplyBuild{ae->args.size(), std::move(remaining)});
        stack.push_back(EvalFrame{ae->func.get()});
        break;
    }
    case NodeKind::FunRef: {
        auto *fr = expr_cast<FunRefExpr>(e);
        results.push_back(std::make_unique<FunRefExpr>(fr->name, fr->arity));
        break;
    }
    case NodeKind::Allocate:
    case NodeKind::Collect:
    case NodeKind::GlobalValue:
        results.push_back(std::make_unique<VoidExpr>());
        break;
    }
}

/// @brief Process continuation frame
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<UnaryExpr>(
            ub->op, std::move(operand)));
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
    } else if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        if (ab->remaining.empty()) {
            // func + all args on results stack
            std::vector<std::unique_ptr<Expr>> args;
            for (size_t i = 0; i < ab->total; ++i) {
                args.push_back(std::move(results.back()));
                results.pop_back();
            }
            std::reverse(args.begin(), args.end());
            auto func = std::move(results.back()); results.pop_back();
            results.push_back(std::make_unique<ApplyExpr>(
                std::move(func), std::move(args)));
        } else {
            const Expr *next = ab->remaining[0];
            std::vector<const Expr *> rest(ab->remaining.begin() + 1,
                                            ab->remaining.end());
            stack.push_back(ApplyBuild{ab->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
        }
    }
}

/// @brief Transform one expression tree
std::unique_ptr<Expr> transform(const Expr *root, const FunMap &funs) {
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    stack.push_back(EvalFrame{root});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, funs, stack, results);
        } else {
            process_cont(frame, stack, results);
        }
    }
    return std::move(results.back());
}

} // namespace

/// @brief Replace VarExpr for function names with FunRefExpr
std::unique_ptr<Program> reveal_functions(const Program &prog) {
    FunMap funs;
    // invariant: funs has entries for defs[0..i)
    for (const auto &def : prog.defs) {
        funs[def.name] = static_cast<int64_t>(def.params.size());
    }

    std::vector<DefNode> new_defs;
    // invariant: new_defs[0..i) transformed
    for (const auto &def : prog.defs) {
        auto new_body = transform(def.body.get(), funs);
        new_defs.push_back(DefNode{def.name, def.params, def.ret_type,
                                    std::move(new_body)});
    }

    auto new_body = transform(prog.body.get(), funs);
    return std::make_unique<Program>(std::move(new_defs), std::move(new_body));
}

} // namespace mc
