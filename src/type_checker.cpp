#include "type_checker.h"

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

using TypeEnv = std::map<std::string, Type>;

struct EvalFrame {
    const Expr *expr;
    TypeEnv env;
};

struct UnaryFrame { UnaryOp op; };

struct BinLhsFrame {
    BinaryOp op;
    const Expr *rhs;
    TypeEnv env;
};

struct BinRhsFrame { BinaryOp op; };

struct IfCondFrame {
    const Expr *then_branch;
    const Expr *else_branch;
    TypeEnv env;
};

struct IfThenFrame {
    const Expr *else_branch;
    TypeEnv env;
};

struct IfElseFrame {};

struct LetBindFrame {
    std::string var;
    const Expr *body;
    TypeEnv env;
};

struct LetBodyFrame {};

struct WhileCondFrame {
    const Expr *body;
    TypeEnv env;
};

struct WhileBodyFrame {};

struct SetBangFrame {
    std::string var;
    TypeEnv env;
};

struct BeginFrame {
    std::vector<const Expr *> remaining;
    TypeEnv env;
};

using Frame = std::variant<EvalFrame, UnaryFrame, BinLhsFrame, BinRhsFrame,
                           IfCondFrame, IfThenFrame, IfElseFrame,
                           LetBindFrame, LetBodyFrame,
                           WhileCondFrame, WhileBodyFrame,
                           SetBangFrame, BeginFrame>;

/// @brief Push eval for type checking
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<Type> &types) {
    const Expr *e = ef.expr;
    const auto &env = ef.env;

    if (dynamic_cast<const IntExpr *>(e) != nullptr) {
        types.push_back(Type::Int);
    } else if (dynamic_cast<const BoolExpr *>(e) != nullptr) {
        types.push_back(Type::Bool);
    } else if (const auto *ve = dynamic_cast<const VarExpr *>(e)) {
        auto it = env.find(ve->name);
        if (it == env.end()) {
            throw TypeError("unbound variable: " + ve->name);
        }
        types.push_back(it->second);
    } else if (dynamic_cast<const ReadExpr *>(e) != nullptr) {
        types.push_back(Type::Int);
    } else if (const auto *ue = dynamic_cast<const UnaryExpr *>(e)) {
        stack.push_back(UnaryFrame{ue->op});
        stack.push_back(EvalFrame{ue->operand.get(), env});
    } else if (const auto *be = dynamic_cast<const BinaryExpr *>(e)) {
        stack.push_back(BinLhsFrame{be->op, be->rhs.get(), env});
        stack.push_back(EvalFrame{be->lhs.get(), env});
    } else if (const auto *ife = dynamic_cast<const IfExpr *>(e)) {
        stack.push_back(IfCondFrame{ife->then_branch.get(),
                                     ife->else_branch.get(), env});
        stack.push_back(EvalFrame{ife->cond.get(), env});
    } else if (const auto *le = dynamic_cast<const LetExpr *>(e)) {
        stack.push_back(LetBindFrame{le->var, le->body.get(), env});
        stack.push_back(EvalFrame{le->init.get(), env});
    } else if (const auto *we = dynamic_cast<const WhileExpr *>(e)) {
        stack.push_back(WhileCondFrame{we->body.get(), env});
        stack.push_back(EvalFrame{we->cond.get(), env});
    } else if (const auto *se = dynamic_cast<const SetBangExpr *>(e)) {
        auto it = env.find(se->var_name);
        if (it == env.end()) {
            throw TypeError("unbound variable in set!: " + se->var_name);
        }
        stack.push_back(SetBangFrame{se->var_name, env});
        stack.push_back(EvalFrame{se->expr.get(), env});
    } else if (const auto *beg = dynamic_cast<const BeginExpr *>(e)) {
        if (beg->exprs.empty()) {
            types.push_back(Type::Void);
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < beg->exprs.size(); ++i) {
                remaining.push_back(beg->exprs[i].get());
            }
            stack.push_back(BeginFrame{std::move(remaining), env});
            stack.push_back(EvalFrame{beg->exprs[0].get(), env});
        }
    } else if (dynamic_cast<const VoidExpr *>(e) != nullptr) {
        types.push_back(Type::Void);
    } else if (const auto *ge = dynamic_cast<const GetExpr *>(e)) {
        auto it = env.find(ge->name);
        if (it == env.end()) {
            throw TypeError("unbound variable: " + ge->name);
        }
        types.push_back(it->second);
    }
}

/// @brief Process continuation for type checking
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<Type> &types) {
    if (auto *uf = std::get_if<UnaryFrame>(&frame)) {
        Type t = types.back(); types.pop_back();
        if (uf->op == UnaryOp::Neg) {
            if (t != Type::Int) throw TypeError("neg requires Int");
            types.push_back(Type::Int);
        } else {
            if (t != Type::Bool) throw TypeError("not requires Bool");
            types.push_back(Type::Bool);
        }
    } else if (auto *bl = std::get_if<BinLhsFrame>(&frame)) {
        stack.push_back(BinRhsFrame{bl->op});
        stack.push_back(EvalFrame{bl->rhs, bl->env});
    } else if (auto *br = std::get_if<BinRhsFrame>(&frame)) {
        Type rhs = types.back(); types.pop_back();
        Type lhs = types.back(); types.pop_back();
        switch (br->op) {
        case BinaryOp::Add: case BinaryOp::Sub:
            if (lhs != Type::Int || rhs != Type::Int)
                throw TypeError("+/- requires Int operands");
            types.push_back(Type::Int);
            break;
        case BinaryOp::And: case BinaryOp::Or:
            if (lhs != Type::Bool || rhs != Type::Bool)
                throw TypeError("and/or requires Bool operands");
            types.push_back(Type::Bool);
            break;
        case BinaryOp::Eq:
            if (lhs != rhs)
                throw TypeError("== requires same type operands");
            types.push_back(Type::Bool);
            break;
        case BinaryOp::Lt: case BinaryOp::Le:
        case BinaryOp::Gt: case BinaryOp::Ge:
            if (lhs != Type::Int || rhs != Type::Int)
                throw TypeError("comparison requires Int operands");
            types.push_back(Type::Bool);
            break;
        }
    } else if (auto *ic = std::get_if<IfCondFrame>(&frame)) {
        Type cond_t = types.back(); types.pop_back();
        if (cond_t != Type::Bool)
            throw TypeError("if condition must be Bool");
        stack.push_back(IfThenFrame{ic->else_branch, ic->env});
        stack.push_back(EvalFrame{ic->then_branch, ic->env});
    } else if (auto *it = std::get_if<IfThenFrame>(&frame)) {
        stack.push_back(IfElseFrame{});
        stack.push_back(EvalFrame{it->else_branch, it->env});
    } else if (std::get_if<IfElseFrame>(&frame) != nullptr) {
        Type else_t = types.back(); types.pop_back();
        Type then_t = types.back(); types.pop_back();
        if (then_t != else_t)
            throw TypeError("if branches must have same type");
        types.push_back(then_t);
    } else if (auto *lb = std::get_if<LetBindFrame>(&frame)) {
        Type init_t = types.back(); types.pop_back();
        TypeEnv new_env = lb->env;
        new_env[lb->var] = init_t;
        stack.push_back(LetBodyFrame{});
        stack.push_back(EvalFrame{lb->body, std::move(new_env)});
    } else if (std::get_if<LetBodyFrame>(&frame) != nullptr) {
        // body type is already on stack, leave it
    } else if (auto *wc = std::get_if<WhileCondFrame>(&frame)) {
        Type cond_t = types.back(); types.pop_back();
        if (cond_t != Type::Bool)
            throw TypeError("while condition must be Bool");
        stack.push_back(WhileBodyFrame{});
        stack.push_back(EvalFrame{wc->body, wc->env});
    } else if (std::get_if<WhileBodyFrame>(&frame) != nullptr) {
        types.pop_back(); // discard body type
        types.push_back(Type::Void);
    } else if (auto *sb = std::get_if<SetBangFrame>(&frame)) {
        Type expr_t = types.back(); types.pop_back();
        // Check that expr type matches the variable's type
        // (env was already checked in push_eval)
        auto it = sb->env.find(sb->var);
        if (it != sb->env.end() && it->second != expr_t) {
            throw TypeError("set! type mismatch for " + sb->var);
        }
        types.push_back(Type::Void);
    } else if (auto *bf = std::get_if<BeginFrame>(&frame)) {
        if (bf->remaining.empty()) {
            // Last expr type is already on stack, leave it
        } else {
            types.pop_back(); // discard non-last expr type
            const Expr *next = bf->remaining[0];
            std::vector<const Expr *> rest(bf->remaining.begin() + 1,
                                            bf->remaining.end());
            stack.push_back(BeginFrame{std::move(rest), bf->env});
            stack.push_back(EvalFrame{next, bf->env});
        }
    }
}

} // namespace

/// @brief Type-check program
Type type_check(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<Type> types;
    stack.push_back(EvalFrame{prog.body.get(), {}});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, types);
        } else {
            process_cont(frame, stack, types);
        }
    }
    return types.back();
}
