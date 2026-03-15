#include "interpreter.h"

#include <istream>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

using Env = std::map<std::string, Value>;

struct EvalFrame { const Expr *expr; Env env; };
struct LetBindFrame { std::string var; const Expr *body; Env env; };
struct UnaryFrame { UnaryOp op; };
struct BinLhsFrame { BinaryOp op; const Expr *rhs; Env env; };
struct BinRhsFrame { BinaryOp op; };
struct IfCondFrame { const Expr *then_br; const Expr *else_br; Env env; };

using Frame = std::variant<EvalFrame, LetBindFrame, UnaryFrame,
                           BinLhsFrame, BinRhsFrame, IfCondFrame>;

/// @brief Get int64_t from Value or throw
int64_t as_int(const Value &v) { return std::get<int64_t>(v); }

/// @brief Get bool from Value or throw
bool as_bool(const Value &v) { return std::get<bool>(v); }

void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<Value> &values, std::istream &in) {
    const Expr *e = ef.expr;
    const auto &env = ef.env;

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
        stack.push_back(EvalFrame{ue->operand.get(), env});
    } else if (const auto *bine = dynamic_cast<const BinaryExpr *>(e)) {
        // Short-circuit for And/Or
        if (bine->op == BinaryOp::And || bine->op == BinaryOp::Or) {
            stack.push_back(IfCondFrame{
                bine->op == BinaryOp::And ? bine->rhs.get() : nullptr,
                bine->op == BinaryOp::Or ? bine->rhs.get() : nullptr,
                env});
            stack.push_back(EvalFrame{bine->lhs.get(), env});
        } else {
            stack.push_back(BinLhsFrame{bine->op, bine->rhs.get(), env});
            stack.push_back(EvalFrame{bine->lhs.get(), env});
        }
    } else if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
        stack.push_back(IfCondFrame{ife->then_branch.get(),
                                     ife->else_branch.get(), env});
        stack.push_back(EvalFrame{ife->cond.get(), env});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        stack.push_back(LetBindFrame{le->var, le->body.get(), env});
        stack.push_back(EvalFrame{le->init.get(), env});
    }
}

void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<Value> &values) {
    if (auto *lf = std::get_if<LetBindFrame>(&frame)) {
        Value val = values.back(); values.pop_back();
        Env new_env = lf->env;
        new_env[lf->var] = val;
        stack.push_back(EvalFrame{lf->body, std::move(new_env)});
    } else if (auto *uf = std::get_if<UnaryFrame>(&frame)) {
        Value v = values.back(); values.pop_back();
        if (uf->op == UnaryOp::Neg) {
            values.push_back(-as_int(v));
        } else {
            values.push_back(!as_bool(v));
        }
    } else if (auto *bf = std::get_if<BinLhsFrame>(&frame)) {
        stack.push_back(BinRhsFrame{bf->op});
        stack.push_back(EvalFrame{bf->rhs, bf->env});
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
                stack.push_back(EvalFrame{ic->else_br, ic->env});
            }
        } else if (ic->else_br == nullptr) {
            // And: if false → false, else → eval rhs
            if (!cond_b) {
                values.push_back(false);
            } else {
                stack.push_back(EvalFrame{ic->then_br, ic->env});
            }
        } else {
            // Normal if
            const Expr *branch = cond_b ? ic->then_br : ic->else_br;
            stack.push_back(EvalFrame{branch, ic->env});
        }
    }
}

} // namespace

Value interpret(const Program &prog, std::istream &in) {
    std::vector<Frame> stack;
    std::vector<Value> values;
    stack.push_back(EvalFrame{prog.body.get(), {}});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, values, in);
        } else {
            process_cont(frame, stack, values);
        }
    }
    return values.back();
}
