#include "rco.h"

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild>;

using Binding = std::pair<std::string, std::unique_ptr<Expr>>;

struct Result {
    std::unique_ptr<Expr> expr;
    std::vector<Binding> bindings;
};

void atomize(Result &res, Need need, int &tmp_counter) {
    if (need != Need::Atom) return;
    bool is_atom = (dynamic_cast<IntExpr *>(res.expr.get()) != nullptr) ||
                   (dynamic_cast<BoolExpr *>(res.expr.get()) != nullptr) ||
                   (dynamic_cast<VarExpr *>(res.expr.get()) != nullptr);
    if (is_atom) return;
    std::string tmp = "tmp." + std::to_string(tmp_counter++);
    res.bindings.push_back({tmp, std::move(res.expr)});
    res.expr = std::make_unique<VarExpr>(tmp);
}

std::unique_ptr<Expr> wrap_bindings(std::unique_ptr<Expr> expr,
                                     std::vector<Binding> &bindings) {
    for (int i = static_cast<int>(bindings.size()) - 1; i >= 0; --i) {
        expr = std::make_unique<LetExpr>(
            std::move(bindings[i].first), std::move(bindings[i].second),
            std::move(expr));
    }
    return expr;
}

void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<Result> &results, int &tmp_counter) {
    const Expr *e = ef.expr;

    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        results.push_back({std::make_unique<IntExpr>(ie->value), {}});
    } else if (const auto *be = dynamic_cast<const BoolExpr *>(e)) {
        results.push_back({std::make_unique<BoolExpr>(be->value), {}});
    } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        results.push_back({std::make_unique<VarExpr>(ve->name), {}});
    } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        Result res = {std::make_unique<ReadExpr>(), {}};
        atomize(res, ef.need, tmp_counter);
        results.push_back(std::move(res));
    } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        stack.push_back(UnaryBuild{ue->op, ef.need});
        stack.push_back(EvalFrame{ue->operand.get(), Need::Atom});
    } else if (const auto *bine = dynamic_cast<const BinaryExpr *>(e)) {
        stack.push_back(BinBuildLhs{bine->op, bine->rhs.get(), ef.need});
        stack.push_back(EvalFrame{bine->lhs.get(), Need::Atom});
    } else if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
        // If: condition in Need::Expr, branches in Need::Expr
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get(), ef.need});
        stack.push_back(EvalFrame{ife->cond.get(), Need::Expr});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get(), Need::Expr});
    } else if (const auto *we = dynamic_cast<const WhileExpr *>(e)) {
        // while is complex, never atomized — branches in Need::Expr
        stack.push_back(WhileBuildCond{we->body.get(), ef.need});
        stack.push_back(EvalFrame{we->cond.get(), Need::Expr});
    } else if (const auto *se = dynamic_cast<const SetBangExpr *>(e)) {
        // set! is complex
        stack.push_back(SetBangBuild{se->var_name, ef.need});
        stack.push_back(EvalFrame{se->expr.get(), Need::Expr});
    } else if (const auto *beg = dynamic_cast<const BeginExpr *>(e)) {
        // begin is complex
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
    } else if (dynamic_cast<const VoidExpr *>(e) != nullptr) {
        // void is atomic
        results.push_back({std::make_unique<VoidExpr>(), {}});
    } else if (const auto *ge = dynamic_cast<const GetExpr *>(e)) {
        // get! is atomic (like VarExpr)
        results.push_back({std::make_unique<GetExpr>(ge->name), {}});
    }
}

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

} // namespace

std::unique_ptr<Program> remove_complex_operands(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<Result> results;
    int tmp_counter = 0;
    stack.push_back(EvalFrame{prog.body.get(), Need::Expr});

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
    auto body = wrap_bindings(std::move(res.expr), res.bindings);
    return std::make_unique<Program>(std::move(body));
}
