#include "interpreter.h"

#include <istream>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

using Env = std::map<std::string, int64_t>;

struct EvalFrame {
    const Expr *expr;
    Env env;
};

struct LetBindFrame {
    std::string var;
    const Expr *body;
    Env env;
};

struct UnaryFrame {
    UnaryOp op;
};

struct BinLhsFrame {
    BinaryOp op;
    const Expr *rhs;
    Env env;
};

struct BinRhsFrame {
    BinaryOp op;
    int64_t lhs_val;
};

using Frame = std::variant<EvalFrame, LetBindFrame, UnaryFrame, BinLhsFrame, BinRhsFrame>;

/// @brief Push eval tasks for a leaf/compound expression
/// @requires ef.expr != nullptr
/// @ensures appropriate frames pushed onto stack
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack, std::vector<int64_t> &values,
               std::istream &in) {
    const Expr *e = ef.expr;
    const auto &env = ef.env;

    if (const auto *ie = dynamic_cast<const IntExpr *>(e)) {
        values.push_back(ie->value);
    } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        values.push_back(env.at(ve->name));
    } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        int64_t val = 0;
        in >> val;
        values.push_back(val);
    } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        stack.push_back(UnaryFrame{ue->op});
        stack.push_back(EvalFrame{ue->operand.get(), env});
    } else if (const auto *be = dynamic_cast<const BinaryExpr *>(e)) {
        stack.push_back(BinLhsFrame{be->op, be->rhs.get(), env});
        stack.push_back(EvalFrame{be->lhs.get(), env});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        stack.push_back(LetBindFrame{le->var, le->body.get(), env});
        stack.push_back(EvalFrame{le->init.get(), env});
    }
}

/// @brief Process a continuation frame after a value is produced
/// @requires values is non-empty for frames needing a value
void process_cont(Frame &frame, std::vector<Frame> &stack, std::vector<int64_t> &values) {
    if (auto *lf = std::get_if<LetBindFrame>(&frame)) {
        int64_t val = values.back();
        values.pop_back();
        Env new_env = lf->env;
        new_env[lf->var] = val;
        stack.push_back(EvalFrame{lf->body, std::move(new_env)});
    } else if (auto *uf = std::get_if<UnaryFrame>(&frame)) {
        int64_t v = values.back();
        values.pop_back();
        (void)uf;
        values.push_back(-v);
    } else if (auto *bf = std::get_if<BinLhsFrame>(&frame)) {
        int64_t lhs_val = values.back();
        values.pop_back();
        stack.push_back(BinRhsFrame{bf->op, lhs_val});
        stack.push_back(EvalFrame{bf->rhs, bf->env});
    } else if (auto *br = std::get_if<BinRhsFrame>(&frame)) {
        int64_t rhs_val = values.back();
        values.pop_back();
        int64_t res = (br->op == BinaryOp::Add) ? (br->lhs_val + rhs_val)
                                                 : (br->lhs_val - rhs_val);
        values.push_back(res);
    }
}

} // namespace

/// @brief Interpret program using explicit stack (no recursion)
/// @requires prog.body != nullptr
/// @ensures result == semantics of prog
int64_t interpret(const Program &prog, std::istream &in) {
    std::vector<Frame> stack;
    std::vector<int64_t> values;

    stack.push_back(EvalFrame{prog.body.get(), {}});

    // decreases stack.size() (each iteration pops 1, pushes bounded frames)
    // invariant values holds results of fully evaluated subexpressions
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
