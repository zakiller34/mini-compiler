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

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild>;

void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results, int &counter) {
    const Expr *e = ef.expr;
    const auto &env = ef.env;

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
        auto it = env.find(ve->name);
        std::string name = (it != env.end()) ? it->second : ve->name;
        results.push_back(std::make_unique<VarExpr>(name));
        break;
    }
    case NodeKind::Read:
        results.push_back(std::make_unique<ReadExpr>());
        break;
    case NodeKind::Unary: {
        auto *ue = static_cast<const UnaryExpr *>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get(), env});
        break;
    }
    case NodeKind::Binary: {
        auto *bine = static_cast<const BinaryExpr *>(e);
        stack.push_back(BinBuildLhs{bine->op, bine->rhs.get(), env});
        stack.push_back(EvalFrame{bine->lhs.get(), env});
        break;
    }
    case NodeKind::If: {
        auto *ife = static_cast<const IfExpr *>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get(), env});
        stack.push_back(EvalFrame{ife->cond.get(), env});
        break;
    }
    case NodeKind::Let: {
        auto *le = static_cast<const LetExpr *>(e);
        std::string new_name = le->var + "." + std::to_string(counter++);
        stack.push_back(LetBuildInit{le->var, new_name, le->body.get(), env});
        stack.push_back(EvalFrame{le->init.get(), env});
        break;
    }
    case NodeKind::While: {
        auto *we = static_cast<const WhileExpr *>(e);
        stack.push_back(WhileBuildCond{we->body.get(), env});
        stack.push_back(EvalFrame{we->cond.get(), env});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = static_cast<const SetBangExpr *>(e);
        auto it = env.find(se->var_name);
        std::string name = (it != env.end()) ? it->second : se->var_name;
        stack.push_back(SetBangBuild{name});
        stack.push_back(EvalFrame{se->expr.get(), env});
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
        auto *ge = static_cast<const GetExpr *>(e);
        auto it = env.find(ge->name);
        std::string name = (it != env.end()) ? it->second : ge->name;
        results.push_back(std::make_unique<GetExpr>(name));
        break;
    }
    }
}

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
    }
}

} // namespace

std::unique_ptr<Program> uniquify(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    int counter = 1;
    stack.push_back(EvalFrame{prog.body.get(), {}});

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
