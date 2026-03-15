#include "shrink.h"

#include <memory>
#include <variant>
#include <vector>

namespace {

struct EvalFrame { const Expr *expr; };
struct UnaryBuild { UnaryOp op; };
struct BinBuildLhs { BinaryOp op; const Expr *rhs; };
struct BinBuildRhs { BinaryOp op; };
struct IfBuildCond { const Expr *then_br; const Expr *else_br; };
struct IfBuildThen { const Expr *else_br; };
struct IfBuildElse {};
struct LetBuildInit { std::string var; const Expr *body; };
struct LetBuildBody { std::string var; };

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody>;

void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results) {
    const Expr *e = ef.expr;
    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        results.push_back(std::make_unique<IntExpr>(ie->value));
    } else if (const auto *be = dynamic_cast<const BoolExpr *>(e)) {
        results.push_back(std::make_unique<BoolExpr>(be->value));
    } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        results.push_back(std::make_unique<VarExpr>(ve->name));
    } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        results.push_back(std::make_unique<ReadExpr>());
    } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
    } else if (const auto *bine = dynamic_cast<const BinaryExpr *>(e)) {
        if (bine->op == BinaryOp::And || bine->op == BinaryOp::Or) {
            // Desugar: And(a,b) → If(a, b, false)
            //          Or(a,b)  → If(a, true, b)
            if (bine->op == BinaryOp::And) {
                stack.push_back(IfBuildCond{bine->rhs.get(), nullptr});
                stack.push_back(EvalFrame{bine->lhs.get()});
            } else {
                stack.push_back(IfBuildCond{nullptr, bine->rhs.get()});
                stack.push_back(EvalFrame{bine->lhs.get()});
            }
        } else {
            stack.push_back(BinBuildLhs{bine->op, bine->rhs.get()});
            stack.push_back(EvalFrame{bine->lhs.get()});
        }
    } else if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
    }
}

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
        // Cond result on stack. Handle And/Or desugaring:
        if (ic->then_br == nullptr) {
            // Or(a, b) → If(cond, true, b) — then_br is nullptr
            stack.push_back(IfBuildThen{nullptr});
            // "then" is BoolExpr(true), push directly
            results.push_back(std::make_unique<BoolExpr>(true));
            stack.push_back(EvalFrame{ic->else_br});
        } else if (ic->else_br == nullptr) {
            // And(a, b) → If(cond, b, false) — else_br is nullptr
            stack.push_back(IfBuildThen{nullptr});
            stack.push_back(EvalFrame{ic->then_br});
        } else {
            // Normal if
            stack.push_back(IfBuildThen{ic->else_br});
            stack.push_back(EvalFrame{ic->then_br});
        }
    } else if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        if (it->else_br == nullptr) {
            // Desugared And: else is false; Or: else already processed
            // Check if this is And (then result on stack, need false else)
            // or Or (true and else both on stack)
            // For And: then_result on stack, push false
            // For Or: true on stack below, else_result on top — we're done
            // Actually: let's just check if there was an else_br to process
            stack.push_back(IfBuildElse{});
            // For And: push BoolExpr(false) as else
            results.push_back(std::make_unique<BoolExpr>(false));
        } else {
            stack.push_back(IfBuildElse{});
            stack.push_back(EvalFrame{it->else_br});
        }
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
    }
}

} // namespace

std::unique_ptr<Program> shrink(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    stack.push_back(EvalFrame{prog.body.get()});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results);
        } else {
            process_cont(frame, stack, results);
        }
    }
    return std::make_unique<Program>(std::move(results.back()));
}
