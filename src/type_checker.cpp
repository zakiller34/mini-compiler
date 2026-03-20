#include "type_checker.h"

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace {

using TypeEnv = std::map<std::string, TypePtr>;

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

struct VectorBuildFrame {
    size_t total;
    std::vector<const Expr *> remaining;
    TypeEnv env;
};

struct VectorRefFrame { int64_t index; };
struct VectorSetVecFrame { int64_t index; const Expr *val; TypeEnv env; };
struct VectorSetValFrame { int64_t index; };
struct VectorLengthFrame {};

using Frame = std::variant<EvalFrame, UnaryFrame, BinLhsFrame, BinRhsFrame,
                           IfCondFrame, IfThenFrame, IfElseFrame,
                           LetBindFrame, LetBodyFrame,
                           WhileCondFrame, WhileBodyFrame,
                           SetBangFrame, BeginFrame,
                           VectorBuildFrame, VectorRefFrame,
                           VectorSetVecFrame, VectorSetValFrame,
                           VectorLengthFrame>;

/// @brief Push eval for type checking
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<TypePtr> &types) {
    const Expr *e = ef.expr;
    const auto &env = ef.env;

    switch (e->kind()) {
    case NodeKind::Int:
        types.push_back(int_type());
        break;
    case NodeKind::Bool:
        types.push_back(bool_type());
        break;
    case NodeKind::Var: {
        auto *ve = expr_cast<VarExpr>(e);
        auto it = env.find(ve->name);
        if (it == env.end()) {
            throw TypeError("unbound variable: " + ve->name);
        }
        types.push_back(it->second);
        break;
    }
    case NodeKind::Read:
        types.push_back(int_type());
        break;
    case NodeKind::Unary: {
        auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryFrame{ue->op});
        stack.push_back(EvalFrame{ue->operand.get(), env});
        break;
    }
    case NodeKind::Binary: {
        auto *be = expr_cast<BinaryExpr>(e);
        stack.push_back(BinLhsFrame{be->op, be->rhs.get(), env});
        stack.push_back(EvalFrame{be->lhs.get(), env});
        break;
    }
    case NodeKind::If: {
        auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfCondFrame{ife->then_branch.get(),
                                     ife->else_branch.get(), env});
        stack.push_back(EvalFrame{ife->cond.get(), env});
        break;
    }
    case NodeKind::Let: {
        auto *le = expr_cast<LetExpr>(e);
        stack.push_back(LetBindFrame{le->var, le->body.get(), env});
        stack.push_back(EvalFrame{le->init.get(), env});
        break;
    }
    case NodeKind::While: {
        auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileCondFrame{we->body.get(), env});
        stack.push_back(EvalFrame{we->cond.get(), env});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = expr_cast<SetBangExpr>(e);
        auto it = env.find(se->var_name);
        if (it == env.end()) {
            throw TypeError("unbound variable in set!: " + se->var_name);
        }
        stack.push_back(SetBangFrame{se->var_name, env});
        stack.push_back(EvalFrame{se->expr.get(), env});
        break;
    }
    case NodeKind::Begin: {
        auto *beg = expr_cast<BeginExpr>(e);
        if (beg->exprs.empty()) {
            types.push_back(void_type());
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < beg->exprs.size(); ++i) {
                remaining.push_back(beg->exprs[i].get());
            }
            stack.push_back(BeginFrame{std::move(remaining), env});
            stack.push_back(EvalFrame{beg->exprs[0].get(), env});
        }
        break;
    }
    case NodeKind::Void:
        types.push_back(void_type());
        break;
    case NodeKind::Get: {
        auto *ge = expr_cast<GetExpr>(e);
        auto it = env.find(ge->name);
        if (it == env.end()) {
            throw TypeError("unbound variable: " + ge->name);
        }
        types.push_back(it->second);
        break;
    }
    case NodeKind::Vector: {
        auto *ve = expr_cast<VectorExpr>(e);
        if (ve->elems.empty()) {
            throw TypeError("empty vectors not supported");
        }
        std::vector<const Expr *> remaining;
        for (size_t i = 1; i < ve->elems.size(); ++i) {
            remaining.push_back(ve->elems[i].get());
        }
        stack.push_back(VectorBuildFrame{ve->elems.size(),
                                          std::move(remaining), env});
        stack.push_back(EvalFrame{ve->elems[0].get(), env});
        break;
    }
    case NodeKind::VectorRef: {
        auto *vr = expr_cast<VectorRefExpr>(e);
        stack.push_back(VectorRefFrame{vr->index});
        stack.push_back(EvalFrame{vr->vec.get(), env});
        break;
    }
    case NodeKind::VectorSet: {
        auto *vs = expr_cast<VectorSetExpr>(e);
        stack.push_back(VectorSetVecFrame{vs->index, vs->val.get(), env});
        stack.push_back(EvalFrame{vs->vec.get(), env});
        break;
    }
    case NodeKind::VectorLength: {
        auto *vl = expr_cast<VectorLengthExpr>(e);
        stack.push_back(VectorLengthFrame{});
        stack.push_back(EvalFrame{vl->vec.get(), env});
        break;
    }
    case NodeKind::Allocate: {
        auto *ae = expr_cast<AllocateExpr>(e);
        types.push_back(ae->type);
        break;
    }
    case NodeKind::Collect:
        types.push_back(void_type());
        break;
    case NodeKind::GlobalValue:
        types.push_back(int_type());
        break;
    }
}

/// @brief Process continuation for type checking
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<TypePtr> &types) {
    if (auto *uf = std::get_if<UnaryFrame>(&frame)) {
        TypePtr t = types.back(); types.pop_back();
        if (uf->op == UnaryOp::Neg) {
            if (*t != *int_type()) throw TypeError("neg requires Int");
            types.push_back(int_type());
        } else {
            if (*t != *bool_type()) throw TypeError("not requires Bool");
            types.push_back(bool_type());
        }
    } else if (auto *bl = std::get_if<BinLhsFrame>(&frame)) {
        stack.push_back(BinRhsFrame{bl->op});
        stack.push_back(EvalFrame{bl->rhs, bl->env});
    } else if (auto *br = std::get_if<BinRhsFrame>(&frame)) {
        TypePtr rhs = types.back(); types.pop_back();
        TypePtr lhs = types.back(); types.pop_back();
        switch (br->op) {
        case BinaryOp::Add: case BinaryOp::Sub:
            if (*lhs != *int_type() || *rhs != *int_type())
                throw TypeError("+/- requires Int operands");
            types.push_back(int_type());
            break;
        case BinaryOp::And: case BinaryOp::Or:
            if (*lhs != *bool_type() || *rhs != *bool_type())
                throw TypeError("and/or requires Bool operands");
            types.push_back(bool_type());
            break;
        case BinaryOp::Eq:
            if (*lhs != *rhs)
                throw TypeError("== requires same type operands");
            types.push_back(bool_type());
            break;
        case BinaryOp::Lt: case BinaryOp::Le:
        case BinaryOp::Gt: case BinaryOp::Ge:
            if (*lhs != *int_type() || *rhs != *int_type())
                throw TypeError("comparison requires Int operands");
            types.push_back(bool_type());
            break;
        }
    } else if (auto *ic = std::get_if<IfCondFrame>(&frame)) {
        TypePtr cond_t = types.back(); types.pop_back();
        if (*cond_t != *bool_type())
            throw TypeError("if condition must be Bool");
        stack.push_back(IfThenFrame{ic->else_branch, ic->env});
        stack.push_back(EvalFrame{ic->then_branch, ic->env});
    } else if (auto *it = std::get_if<IfThenFrame>(&frame)) {
        stack.push_back(IfElseFrame{});
        stack.push_back(EvalFrame{it->else_branch, it->env});
    } else if (std::get_if<IfElseFrame>(&frame) != nullptr) {
        TypePtr else_t = types.back(); types.pop_back();
        TypePtr then_t = types.back(); types.pop_back();
        if (*then_t != *else_t)
            throw TypeError("if branches must have same type");
        types.push_back(then_t);
    } else if (auto *lb = std::get_if<LetBindFrame>(&frame)) {
        TypePtr init_t = types.back(); types.pop_back();
        TypeEnv new_env = lb->env;
        new_env[lb->var] = init_t;
        stack.push_back(LetBodyFrame{});
        stack.push_back(EvalFrame{lb->body, std::move(new_env)});
    } else if (std::get_if<LetBodyFrame>(&frame) != nullptr) {
        // body type is already on stack, leave it
    } else if (auto *wc = std::get_if<WhileCondFrame>(&frame)) {
        TypePtr cond_t = types.back(); types.pop_back();
        if (*cond_t != *bool_type())
            throw TypeError("while condition must be Bool");
        stack.push_back(WhileBodyFrame{});
        stack.push_back(EvalFrame{wc->body, wc->env});
    } else if (std::get_if<WhileBodyFrame>(&frame) != nullptr) {
        types.pop_back(); // discard body type
        types.push_back(void_type());
    } else if (auto *sb = std::get_if<SetBangFrame>(&frame)) {
        TypePtr expr_t = types.back(); types.pop_back();
        auto it = sb->env.find(sb->var);
        if (it != sb->env.end() && *it->second != *expr_t) {
            throw TypeError("set! type mismatch for " + sb->var);
        }
        types.push_back(void_type());
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
    } else if (auto *vb = std::get_if<VectorBuildFrame>(&frame)) {
        if (vb->remaining.empty()) {
            // All elem types on stack
            std::vector<TypePtr> elem_ts;
            for (size_t i = 0; i < vb->total; ++i) {
                elem_ts.push_back(types.back());
                types.pop_back();
            }
            // Reverse because they were pushed first-to-last
            std::reverse(elem_ts.begin(), elem_ts.end());
            types.push_back(vector_type(std::move(elem_ts)));
        } else {
            const Expr *next = vb->remaining[0];
            std::vector<const Expr *> rest(vb->remaining.begin() + 1,
                                            vb->remaining.end());
            stack.push_back(VectorBuildFrame{vb->total, std::move(rest),
                                              vb->env});
            stack.push_back(EvalFrame{next, vb->env});
        }
    } else if (auto *vr = std::get_if<VectorRefFrame>(&frame)) {
        TypePtr vec_t = types.back(); types.pop_back();
        if (!is_vector_type(vec_t))
            throw TypeError("vector-ref requires Vector type");
        if (vr->index < 0 ||
            vr->index >= static_cast<int64_t>(vec_t->elem_types.size()))
            throw TypeError("vector-ref index out of bounds");
        types.push_back(vec_t->elem_types[static_cast<size_t>(vr->index)]);
    } else if (auto *vsv = std::get_if<VectorSetVecFrame>(&frame)) {
        // vec type is on stack, now eval val
        stack.push_back(VectorSetValFrame{vsv->index});
        stack.push_back(EvalFrame{vsv->val, vsv->env});
    } else if (auto *vs = std::get_if<VectorSetValFrame>(&frame)) {
        TypePtr val_t = types.back(); types.pop_back();
        TypePtr vec_t = types.back(); types.pop_back();
        if (!is_vector_type(vec_t))
            throw TypeError("vector-set! requires Vector type");
        if (vs->index < 0 ||
            vs->index >= static_cast<int64_t>(vec_t->elem_types.size()))
            throw TypeError("vector-set! index out of bounds");
        auto expected = vec_t->elem_types[static_cast<size_t>(vs->index)];
        if (*val_t != *expected)
            throw TypeError("vector-set! type mismatch");
        types.push_back(void_type());
    } else if (std::get_if<VectorLengthFrame>(&frame) != nullptr) {
        TypePtr vec_t = types.back(); types.pop_back();
        if (!is_vector_type(vec_t))
            throw TypeError("vector-length requires Vector type");
        types.push_back(int_type());
    }
}

} // namespace

/// @brief Type-check program
TypePtr type_check(const Program &prog) {
    std::vector<Frame> stack;
    std::vector<TypePtr> types;
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
