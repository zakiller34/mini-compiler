#include "type_checker.h"

#include <algorithm>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace mc {

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
    const Expr *then_branch;
    const Expr *else_branch;
    TypeEnv env;
};

/// `then_br`/`else_br` are kept so a branch that is `Exit` (which never
/// returns) can be excluded from the same-type check.
struct IfElseFrame {
    const Expr *then_br;
    const Expr *else_br;
};

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
struct ApplyBuildFrame {
    size_t total_args;
    std::vector<const Expr *> remaining;
    TypeEnv env;
};
struct LambdaBodyFrame {
    std::vector<TypePtr> param_types;
    TypePtr ret_type;
};
struct ProcArityFrame {};

// -- L_Any frames (Siek 2023, figure 9.6) --

struct InjectFrame { TypePtr ftype; };
struct ProjectFrame { TypePtr ftype; };
struct TypePredFrame {};
struct AnyRefVecFrame { const Expr *idx; TypeEnv env; };
struct AnyRefIdxFrame {};
struct AnySetVecFrame { const Expr *idx; const Expr *val; TypeEnv env; };
struct AnySetIdxFrame { const Expr *val; TypeEnv env; };
struct AnySetValFrame {};
struct AnyLengthFrame {};
struct MakeAnyFrame {};
struct TagOfAnyFrame {};
struct ValueOfFrame { TypePtr ftype; };

using Frame = std::variant<EvalFrame, UnaryFrame, BinLhsFrame, BinRhsFrame,
                           IfCondFrame, IfThenFrame, IfElseFrame,
                           LetBindFrame, LetBodyFrame,
                           WhileCondFrame, WhileBodyFrame,
                           SetBangFrame, BeginFrame,
                           VectorBuildFrame, VectorRefFrame,
                           VectorSetVecFrame, VectorSetValFrame,
                           VectorLengthFrame, ApplyBuildFrame,
                           LambdaBodyFrame, ProcArityFrame,
                           InjectFrame, ProjectFrame, TypePredFrame,
                           AnyRefVecFrame, AnyRefIdxFrame,
                           AnySetVecFrame, AnySetIdxFrame, AnySetValFrame,
                           AnyLengthFrame, MakeAnyFrame, TagOfAnyFrame,
                           ValueOfFrame>;

/// @brief Push eval frames for the L_Any node kinds (enum-switch FSM)
/// @requires e != nullptr
/// @ensures returns false if e is not an L_Any node
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool push_eval_any(const Expr *e, const TypeEnv &env,
                   std::vector<Frame> &stack) {
    switch (e->kind()) {
    case NodeKind::Inject: {
        const auto *ie = expr_cast<InjectExpr>(e);
        stack.push_back(InjectFrame{ie->ftype});
        stack.push_back(EvalFrame{ie->expr.get(), env});
        return true;
    }
    case NodeKind::Project: {
        const auto *pe = expr_cast<ProjectExpr>(e);
        stack.push_back(ProjectFrame{pe->ftype});
        stack.push_back(EvalFrame{pe->expr.get(), env});
        return true;
    }
    case NodeKind::TypePredicate: {
        const auto *tp = expr_cast<TypePredExpr>(e);
        stack.push_back(TypePredFrame{});
        stack.push_back(EvalFrame{tp->expr.get(), env});
        return true;
    }
    case NodeKind::AnyVectorRef: {
        const auto *ar = expr_cast<AnyVectorRefExpr>(e);
        stack.push_back(AnyRefVecFrame{ar->idx.get(), env});
        stack.push_back(EvalFrame{ar->vec.get(), env});
        return true;
    }
    case NodeKind::AnyVectorSet: {
        const auto *as = expr_cast<AnyVectorSetExpr>(e);
        stack.push_back(AnySetVecFrame{as->idx.get(), as->val.get(), env});
        stack.push_back(EvalFrame{as->vec.get(), env});
        return true;
    }
    case NodeKind::AnyVectorLength: {
        const auto *al = expr_cast<AnyVectorLengthExpr>(e);
        stack.push_back(AnyLengthFrame{});
        stack.push_back(EvalFrame{al->vec.get(), env});
        return true;
    }
    case NodeKind::MakeAny: {
        const auto *ma = expr_cast<MakeAnyExpr>(e);
        stack.push_back(MakeAnyFrame{});
        stack.push_back(EvalFrame{ma->expr.get(), env});
        return true;
    }
    case NodeKind::TagOfAny: {
        const auto *ta = expr_cast<TagOfAnyExpr>(e);
        stack.push_back(TagOfAnyFrame{});
        stack.push_back(EvalFrame{ta->expr.get(), env});
        return true;
    }
    case NodeKind::ValueOf: {
        const auto *vo = expr_cast<ValueOfExpr>(e);
        stack.push_back(ValueOfFrame{vo->ftype});
        stack.push_back(EvalFrame{vo->expr.get(), env});
        return true;
    }
    default:
        return false;
    }
}

/// @brief Push eval for type checking
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
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
    case NodeKind::FunRef: {
        auto *fr = expr_cast<FunRefExpr>(e);
        auto it = env.find(fr->name);
        if (it == env.end()) {
            throw TypeError("unbound function: " + fr->name);
        }
        types.push_back(it->second);
        break;
    }
    case NodeKind::Apply: {
        auto *ae = expr_cast<ApplyExpr>(e);
        // Push ApplyBuildFrame, then eval func, then eval all args
        std::vector<const Expr *> remaining;
        for (size_t i = 0; i < ae->args.size(); ++i) {
            remaining.push_back(ae->args[i].get());
        }
        stack.push_back(ApplyBuildFrame{ae->args.size(),
                                         std::move(remaining), env});
        stack.push_back(EvalFrame{ae->func.get(), env});
        break;
    }
    case NodeKind::Lambda: {
        auto *la = expr_cast<LambdaExpr>(e);
        TypeEnv body_env = env;
        std::vector<TypePtr> param_types;
        // invariant: body_env extended with params[0..i), param_types filled
        for (const auto &p : la->params) {
            body_env[p.first] = p.second;
            param_types.push_back(p.second);
        }
        stack.push_back(
            LambdaBodyFrame{std::move(param_types), la->ret_type});
        stack.push_back(EvalFrame{la->body.get(), std::move(body_env)});
        break;
    }
    case NodeKind::ProcArity: {
        auto *pa = expr_cast<ProcArityExpr>(e);
        stack.push_back(ProcArityFrame{});
        stack.push_back(EvalFrame{pa->expr.get(), env});
        break;
    }
    case NodeKind::Exit:
        // Exit halts, so its type is never observed; IfElseFrame skips the
        // same-type check whenever a branch is Exit.
        types.push_back(void_type());
        break;
    default:
        if (!push_eval_any(e, env, stack)) {
            throw TypeError("type_check: unexpected node kind");
        }
        break;
    }
}

/// @brief Continuations for Inject/Project/type predicates
/// @ensures returns false if frame is not one of these
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool cont_cast(Frame &frame, std::vector<TypePtr> &types) {
    if (auto *inf = std::get_if<InjectFrame>(&frame)) {
        TypePtr t = types.back(); types.pop_back();
        if (!is_flat_type(inf->ftype)) {
            throw TypeError("inject: not a flat type: " + inf->ftype->dump());
        }
        if (*t != *inf->ftype) {
            throw TypeError("inject: operand is " + t->dump() + ", expected " +
                            inf->ftype->dump());
        }
        types.push_back(any_type());
        return true;
    }
    if (auto *pf = std::get_if<ProjectFrame>(&frame)) {
        TypePtr t = types.back(); types.pop_back();
        if (!is_flat_type(pf->ftype)) {
            throw TypeError("project: not a flat type: " + pf->ftype->dump());
        }
        if (!is_any_type(t)) {
            throw TypeError("project: operand must be Any, got " + t->dump());
        }
        types.push_back(pf->ftype);
        return true;
    }
    if (std::get_if<TypePredFrame>(&frame) != nullptr) {
        TypePtr t = types.back(); types.pop_back();
        if (!is_any_type(t)) {
            throw TypeError("type predicate requires Any, got " + t->dump());
        }
        types.push_back(bool_type());
        return true;
    }
    return false;
}

/// @brief Continuations for the any-vector-* operations
/// @ensures returns false if frame is not one of these
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool cont_any_vector(Frame &frame, std::vector<Frame> &stack,
                     std::vector<TypePtr> &types) {
    if (auto *rv = std::get_if<AnyRefVecFrame>(&frame)) {
        TypePtr vec_t = types.back(); types.pop_back();
        if (!is_any_type(vec_t)) {
            throw TypeError("any-vector-ref requires Any tuple");
        }
        stack.push_back(AnyRefIdxFrame{});
        stack.push_back(EvalFrame{rv->idx, rv->env});
        return true;
    }
    if (std::get_if<AnyRefIdxFrame>(&frame) != nullptr) {
        TypePtr idx_t = types.back(); types.pop_back();
        if (*idx_t != *int_type()) {
            throw TypeError("any-vector-ref index must be Int");
        }
        types.push_back(any_type());
        return true;
    }
    if (auto *sv = std::get_if<AnySetVecFrame>(&frame)) {
        TypePtr vec_t = types.back(); types.pop_back();
        if (!is_any_type(vec_t)) {
            throw TypeError("any-vector-set! requires Any tuple");
        }
        stack.push_back(AnySetIdxFrame{sv->val, sv->env});
        stack.push_back(EvalFrame{sv->idx, sv->env});
        return true;
    }
    if (auto *si = std::get_if<AnySetIdxFrame>(&frame)) {
        TypePtr idx_t = types.back(); types.pop_back();
        if (*idx_t != *int_type()) {
            throw TypeError("any-vector-set! index must be Int");
        }
        stack.push_back(AnySetValFrame{});
        stack.push_back(EvalFrame{si->val, si->env});
        return true;
    }
    if (std::get_if<AnySetValFrame>(&frame) != nullptr) {
        TypePtr val_t = types.back(); types.pop_back();
        if (!is_any_type(val_t)) {
            throw TypeError("any-vector-set! value must be Any");
        }
        types.push_back(void_type());
        return true;
    }
    if (std::get_if<AnyLengthFrame>(&frame) != nullptr) {
        TypePtr vec_t = types.back(); types.pop_back();
        if (!is_any_type(vec_t)) {
            throw TypeError("any-vector-length requires Any");
        }
        types.push_back(int_type());
        return true;
    }
    return false;
}

/// @brief Continuations for the post-reveal_casts nodes
/// @ensures returns false if frame is not one of these
bool cont_revealed(Frame &frame, std::vector<TypePtr> &types) {
    if (std::get_if<MakeAnyFrame>(&frame) != nullptr) {
        types.pop_back();
        types.push_back(any_type());
        return true;
    }
    if (std::get_if<TagOfAnyFrame>(&frame) != nullptr) {
        TypePtr t = types.back(); types.pop_back();
        if (!is_any_type(t)) {
            throw TypeError("tag-of-any requires Any, got " + t->dump());
        }
        types.push_back(int_type());
        return true;
    }
    if (auto *vf = std::get_if<ValueOfFrame>(&frame)) {
        TypePtr t = types.back(); types.pop_back();
        if (!is_any_type(t)) {
            throw TypeError("value-of requires Any, got " + t->dump());
        }
        types.push_back(vf->ftype);
        return true;
    }
    return false;
}

/// @brief Process continuation for type checking
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
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
        stack.push_back(IfThenFrame{ic->then_branch, ic->else_branch,
                                     ic->env});
        stack.push_back(EvalFrame{ic->then_branch, ic->env});
    } else if (auto *it = std::get_if<IfThenFrame>(&frame)) {
        stack.push_back(IfElseFrame{it->then_branch, it->else_branch});
        stack.push_back(EvalFrame{it->else_branch, it->env});
    } else if (auto *ie = std::get_if<IfElseFrame>(&frame)) {
        TypePtr else_t = types.back(); types.pop_back();
        TypePtr then_t = types.back(); types.pop_back();
        // Exit never returns, so it unifies with the other branch
        if (ie->else_br->kind() == NodeKind::Exit) {
            types.push_back(then_t);
        } else if (ie->then_br->kind() == NodeKind::Exit) {
            types.push_back(else_t);
        } else if (*then_t != *else_t) {
            throw TypeError("if branches must have same type");
        } else {
            types.push_back(then_t);
        }
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
    } else if (auto *ab = std::get_if<ApplyBuildFrame>(&frame)) {
        if (ab->remaining.empty()) {
            // All arg types + func type on stack
            // Stack: func_type, arg0_type, ..., argN_type (top)
            std::vector<TypePtr> arg_types;
            for (size_t i = 0; i < ab->total_args; ++i) {
                arg_types.push_back(types.back());
                types.pop_back();
            }
            std::reverse(arg_types.begin(), arg_types.end());
            TypePtr func_t = types.back(); types.pop_back();
            if (!is_fun_type(func_t)) {
                throw TypeError("apply: not a function type");
            }
            // func_t->elem_types = [param0, ..., paramN, ret_type]
            size_t n_params = func_t->elem_types.size() - 1;
            if (arg_types.size() != n_params) {
                throw TypeError("apply: wrong number of arguments");
            }
            // invariant: checked arg_types[0..i) match params[0..i)
            for (size_t i = 0; i < n_params; ++i) {
                if (*arg_types[i] != *func_t->elem_types[i]) {
                    throw TypeError("apply: argument type mismatch");
                }
            }
            types.push_back(func_t->elem_types.back());
        } else {
            const Expr *next = ab->remaining[0];
            std::vector<const Expr *> rest(ab->remaining.begin() + 1,
                                            ab->remaining.end());
            stack.push_back(ApplyBuildFrame{ab->total_args,
                                             std::move(rest), ab->env});
            stack.push_back(EvalFrame{next, ab->env});
        }
    } else if (auto *lbf = std::get_if<LambdaBodyFrame>(&frame)) {
        TypePtr body_t = types.back(); types.pop_back();
        if (*body_t != *lbf->ret_type) {
            throw TypeError("lambda body type does not match return type");
        }
        types.push_back(fun_type(lbf->param_types, lbf->ret_type));
    } else if (std::get_if<ProcArityFrame>(&frame) != nullptr) {
        TypePtr t = types.back(); types.pop_back();
        if (!is_fun_type(t)) {
            throw TypeError("procedure_arity requires a function");
        }
        types.push_back(int_type());
    } else if (!cont_cast(frame, types) &&
               !cont_any_vector(frame, stack, types) &&
               !cont_revealed(frame, types)) {
        throw TypeError("type_check: unexpected continuation frame");
    }
}

/// @brief Run type-checking loop with given initial env
TypePtr run_check(const Expr *expr, const TypeEnv &env) {
    std::vector<Frame> stack;
    std::vector<TypePtr> types;
    stack.push_back(EvalFrame{expr, env});

    // The node most recently entered. Errors raised while checking a node get
    // its position; errors raised in a continuation frame (once operands are
    // already typed) get the position of the last operand entered, which is
    // approximate but still points into the offending expression.
    const Expr *current = expr;

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        try {
            if (auto *ef = std::get_if<EvalFrame>(&frame)) {
                current = ef->expr;
                push_eval(*ef, stack, types);
            } else {
                process_cont(frame, stack, types);
            }
        } catch (const TypeError &e) {
            if (e.loc.known() || current == nullptr) throw;
            throw TypeError(e.what(), current->loc);
        }
    }
    return types.back();
}

} // namespace

/// @brief Type-check program
TypePtr type_check(const Program &prog) {
    // Build env from all defs (supports mutual recursion)
    std::map<std::string, TypePtr> env;
    // invariant: env has fun types for defs[0..i)
    for (const auto &def : prog.defs) {
        std::vector<TypePtr> param_types;
        for (const auto &p : def.params) {
            param_types.push_back(p.second);
        }
        env[def.name] = fun_type(std::move(param_types), def.ret_type);
    }

    // Check each def body
    // invariant: defs[0..i) type-checked
    for (const auto &def : prog.defs) {
        std::map<std::string, TypePtr> body_env = env;
        for (const auto &p : def.params) {
            body_env[p.first] = p.second;
        }
        auto body_t = run_check(def.body.get(), body_env);
        if (*body_t != *def.ret_type) {
            throw TypeError("return type mismatch in " + def.name,
                            def.body->loc);
        }
    }

    return run_check(prog.body.get(), env);
}

/// @brief Type-check expression with given env
TypePtr type_check_expr(const Expr *expr,
                        const std::map<std::string, TypePtr> &env) {
    return run_check(expr, env);
}

} // namespace mc
