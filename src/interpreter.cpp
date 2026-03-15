#include "interpreter.h"

#include <istream>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

/// Flat mutable environment — correct post-uniquify since all names unique.
using Env = std::map<std::string, Value>;

struct EvalFrame { const Expr *expr; };
struct LetBindFrame { std::string var; const Expr *body; };
struct UnaryFrame { UnaryOp op; };
struct BinLhsFrame { BinaryOp op; const Expr *rhs; };
struct BinRhsFrame { BinaryOp op; };
struct IfCondFrame { const Expr *then_br; const Expr *else_br; };
struct WhileCondFrame { const Expr *cond; const Expr *body; };
struct WhileBodyFrame { const Expr *cond; const Expr *body; };
struct SetBangFrame { std::string var; };
struct BeginFrame { std::vector<const Expr *> remaining; };

using Frame = std::variant<EvalFrame, LetBindFrame, UnaryFrame,
                           BinLhsFrame, BinRhsFrame, IfCondFrame,
                           WhileCondFrame, WhileBodyFrame,
                           SetBangFrame, BeginFrame>;

/// @brief Get int64_t from Value or throw
int64_t as_int(const Value &v) { return std::get<int64_t>(v); }

/// @brief Get bool from Value or throw
bool as_bool(const Value &v) { return std::get<bool>(v); }

/// @brief Evaluate leaf or push continuation frames
/// @requires e != nullptr
void push_eval(const Expr *e, Env &env, std::vector<Frame> &stack,
               std::vector<Value> &values, std::istream &in) {
    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        values.push_back(ie->value);
    } else if (const auto *be = dynamic_cast<const BoolExpr *>(e)) {
        values.push_back(be->value);
    } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        values.push_back(env.at(ve->name));
    } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        int64_t val = 0;
        in >> val;
        values.push_back(val);
    } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        stack.push_back(UnaryFrame{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
    } else if (const auto *bine = dynamic_cast<const BinaryExpr *>(e)) {
        if (bine->op == BinaryOp::And || bine->op == BinaryOp::Or) {
            stack.push_back(IfCondFrame{
                bine->op == BinaryOp::And ? bine->rhs.get() : nullptr,
                bine->op == BinaryOp::Or ? bine->rhs.get() : nullptr});
            stack.push_back(EvalFrame{bine->lhs.get()});
        } else {
            stack.push_back(BinLhsFrame{bine->op, bine->rhs.get()});
            stack.push_back(EvalFrame{bine->lhs.get()});
        }
    } else if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
        stack.push_back(IfCondFrame{ife->then_branch.get(),
                                     ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        stack.push_back(LetBindFrame{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
    } else if (const auto *we = dynamic_cast<const WhileExpr *>(e)) {
        stack.push_back(WhileCondFrame{we->cond.get(), we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
    } else if (const auto *se = dynamic_cast<const SetBangExpr *>(e)) {
        stack.push_back(SetBangFrame{se->var_name});
        stack.push_back(EvalFrame{se->expr.get()});
    } else if (const auto *beg = dynamic_cast<const BeginExpr *>(e)) {
        if (beg->exprs.empty()) {
            values.push_back(std::monostate{});
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < beg->exprs.size(); ++i) {
                remaining.push_back(beg->exprs[i].get());
            }
            stack.push_back(BeginFrame{std::move(remaining)});
            stack.push_back(EvalFrame{beg->exprs[0].get()});
        }
    } else if (dynamic_cast<const VoidExpr *>(e) != nullptr) {
        values.push_back(std::monostate{});
    } else if (const auto *ge = dynamic_cast<const GetExpr *>(e)) {
        values.push_back(env.at(ge->name));
    }
}

/// @brief Process continuation frame
void process_cont(Frame &frame, Env &env, std::vector<Frame> &stack,
                  std::vector<Value> &values) {
    if (auto *lf = std::get_if<LetBindFrame>(&frame)) {
        Value val = values.back(); values.pop_back();
        env[lf->var] = val;
        stack.push_back(EvalFrame{lf->body});
    } else if (auto *uf = std::get_if<UnaryFrame>(&frame)) {
        Value v = values.back(); values.pop_back();
        if (uf->op == UnaryOp::Neg) {
            values.push_back(-as_int(v));
        } else {
            values.push_back(!as_bool(v));
        }
    } else if (auto *bf = std::get_if<BinLhsFrame>(&frame)) {
        stack.push_back(BinRhsFrame{bf->op});
        stack.push_back(EvalFrame{bf->rhs});
    } else if (auto *br = std::get_if<BinRhsFrame>(&frame)) {
        Value rhs = values.back(); values.pop_back();
        Value lhs = values.back(); values.pop_back();
        switch (br->op) {
        case BinaryOp::Add:
            values.push_back(as_int(lhs) + as_int(rhs)); break;
        case BinaryOp::Sub:
            values.push_back(as_int(lhs) - as_int(rhs)); break;
        case BinaryOp::Eq:
            values.push_back(lhs == rhs); break;
        case BinaryOp::Lt:
            values.push_back(as_int(lhs) < as_int(rhs)); break;
        case BinaryOp::Le:
            values.push_back(as_int(lhs) <= as_int(rhs)); break;
        case BinaryOp::Gt:
            values.push_back(as_int(lhs) > as_int(rhs)); break;
        case BinaryOp::Ge:
            values.push_back(as_int(lhs) >= as_int(rhs)); break;
        case BinaryOp::And: case BinaryOp::Or:
            break; // handled via IfCondFrame
        }
    } else if (auto *ic = std::get_if<IfCondFrame>(&frame)) {
        Value cond = values.back(); values.pop_back();
        bool cond_b = as_bool(cond);
        if (ic->then_br == nullptr) {
            // Or: if true → true, else → eval rhs
            if (cond_b) {
                values.push_back(true);
            } else {
                stack.push_back(EvalFrame{ic->else_br});
            }
        } else if (ic->else_br == nullptr) {
            // And: if false → false, else → eval rhs
            if (!cond_b) {
                values.push_back(false);
            } else {
                stack.push_back(EvalFrame{ic->then_br});
            }
        } else {
            const Expr *branch = cond_b ? ic->then_br : ic->else_br;
            stack.push_back(EvalFrame{branch});
        }
    } else if (auto *wc = std::get_if<WhileCondFrame>(&frame)) {
        Value cond = values.back(); values.pop_back();
        if (as_bool(cond)) {
            // Re-push while loop (cond check after body)
            stack.push_back(WhileBodyFrame{wc->cond, wc->body});
            stack.push_back(EvalFrame{wc->body});
        } else {
            values.push_back(std::monostate{}); // while returns void
        }
    } else if (auto *wb = std::get_if<WhileBodyFrame>(&frame)) {
        values.pop_back(); // discard body result
        // Re-evaluate condition
        stack.push_back(WhileCondFrame{wb->cond, wb->body});
        stack.push_back(EvalFrame{wb->cond});
    } else if (auto *sb = std::get_if<SetBangFrame>(&frame)) {
        Value val = values.back(); values.pop_back();
        env[sb->var] = val;
        values.push_back(std::monostate{}); // set! returns void
    } else if (auto *bf = std::get_if<BeginFrame>(&frame)) {
        if (bf->remaining.empty()) {
            // Last expr value is on stack, leave it
        } else {
            values.pop_back(); // discard non-last expr value
            const Expr *next = bf->remaining[0];
            std::vector<const Expr *> rest(bf->remaining.begin() + 1,
                                            bf->remaining.end());
            stack.push_back(BeginFrame{std::move(rest)});
            stack.push_back(EvalFrame{next});
        }
    }
}

} // namespace

Value interpret(const Program &prog, std::istream &in) {
    std::vector<Frame> stack;
    std::vector<Value> values;
    Env env;
    stack.push_back(EvalFrame{prog.body.get()});

    // decreases: termination relies on program termination
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(ef->expr, env, stack, values, in);
        } else {
            process_cont(frame, env, stack, values);
        }
    }
    return values.back();
}
