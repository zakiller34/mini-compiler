#include "interpreter.h"

#include <algorithm>
#include <istream>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mc {

namespace {

/// Flat environment of shared variable cells — correct post-uniquify since all
/// names are unique. Cells (shared_ptr<Value>) let closures share mutable state
/// with the scope that defined the captured variable.
using Env = std::map<std::string, std::shared_ptr<Value>>;

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
struct VectorBuildFrame { size_t total; std::vector<const Expr *> remaining; };
struct VectorRefVecFrame { int64_t index; };
struct VectorSetVecFrame { int64_t index; const Expr *val; };
struct VectorSetValFrame { int64_t index; };
struct VectorLengthVecFrame {};
struct ApplyFuncFrame {
    size_t total_args;
    std::vector<const Expr *> remaining;
};
struct ApplyArgsFrame {
    size_t total_args;
};
struct ProcArityFrame {};
/// Restores the caller's environment after a closure body finishes.
struct RestoreEnvFrame { std::map<std::string, std::shared_ptr<Value>> saved; };

// -- L_Any frames (Siek 2023, figure 9.8) --

struct InjectFrame { TypePtr ftype; };
struct ProjectFrame { TypePtr ftype; };
struct TypePredFrame { TypePred pred; };
struct TagOfAnyFrame {};
struct ValueOfFrame {};
struct AnyRefVecFrame { const Expr *idx; };
struct AnyRefIdxFrame {};
struct AnySetVecFrame { const Expr *idx; const Expr *val; };
struct AnySetIdxFrame { const Expr *val; };
struct AnySetValFrame {};
struct AnyLengthFrame {};

using Frame = std::variant<EvalFrame, LetBindFrame, UnaryFrame,
                           BinLhsFrame, BinRhsFrame, IfCondFrame,
                           WhileCondFrame, WhileBodyFrame,
                           SetBangFrame, BeginFrame,
                           VectorBuildFrame, VectorRefVecFrame,
                           VectorSetVecFrame, VectorSetValFrame,
                           VectorLengthVecFrame,
                           ApplyFuncFrame, ApplyArgsFrame,
                           ProcArityFrame, RestoreEnvFrame,
                           InjectFrame, ProjectFrame, TypePredFrame,
                           TagOfAnyFrame, ValueOfFrame,
                           AnyRefVecFrame, AnyRefIdxFrame,
                           AnySetVecFrame, AnySetIdxFrame, AnySetValFrame,
                           AnyLengthFrame>;

/// @brief Get int64_t from Value or throw
int64_t as_int(const Value &v) { return std::get<int64_t>(v); }

/// @brief Get bool from Value or throw
bool as_bool(const Value &v) { return std::get<bool>(v); }

/// @brief Get tuple from Value or throw
Tuple as_tuple(const Value &v) { return std::get<Tuple>(v); }

// -- Tagged (Any) values --

/// @brief Runtime tag of an untagged value
/// @ensures matches tagof() on the value's static type
TypePred tag_of_value(const Value &v) {
    if (std::holds_alternative<int64_t>(v)) return TypePred::Integer;
    if (std::holds_alternative<bool>(v)) return TypePred::Boolean;
    if (std::holds_alternative<Tuple>(v)) return TypePred::Vector;
    if (std::holds_alternative<FunctionValue>(v) ||
        std::holds_alternative<ClosureRef>(v)) {
        return TypePred::Procedure;
    }
    return TypePred::Void;
}

/// @brief The predicate a flat type answers to
/// @requires is_flat_type(t)
TypePred pred_of_type(const TypePtr &t) {
    switch (t->kind) {
    case TypeKind::Int: return TypePred::Integer;
    case TypeKind::Bool: return TypePred::Boolean;
    case TypeKind::Vector: return TypePred::Vector;
    case TypeKind::Function: return TypePred::Procedure;
    default: return TypePred::Void;
    }
}

/// @brief Unwrap a tagged value, or trap
/// @ensures result is the payload; throws TrappedError if v is not tagged
const Value &untag(const Value &v) {
    const auto *t = std::get_if<TaggedValue>(&v);
    if (t == nullptr || !*t) throw TrappedError("expected a tagged value");
    return (*t)->value;
}

/// @brief Structural equality that sees through tags
/// @ensures tagged values are equal iff their tags and payloads are; this
///          mirrors the compiled `cmpq` on two tagged 64-bit words
bool values_equal(const Value &a, const Value &b) {
    const auto *ta = std::get_if<TaggedValue>(&a);
    const auto *tb = std::get_if<TaggedValue>(&b);
    if (ta == nullptr || tb == nullptr) return a == b;
    if (!*ta || !*tb) return *ta == *tb;
    return (*ta)->tag == (*tb)->tag && (*ta)->value == (*tb)->value;
}

/// @brief Arity of a procedure value, or -1
int64_t value_arity(const Value &v) {
    if (const auto *cl = std::get_if<ClosureRef>(&v)) return (*cl)->arity;
    if (const auto *f = std::get_if<FunctionValue>(&v)) return f->arity;
    return -1;
}

/// @brief Check that a projection target's shape matches the value
/// @requires is_flat_type(t)
/// @ensures throws TrappedError on a length/arity mismatch
void check_shape(const Value &inner, const TypePtr &t) {
    auto n = static_cast<int64_t>(t->elem_types.size());
    if (is_vector_type(t)) {
        if (static_cast<int64_t>(as_tuple(inner)->elems.size()) != n) {
            throw TrappedError("project: tuple length mismatch");
        }
    } else if (is_fun_type(t) && value_arity(inner) != n - 1) {
        throw TrappedError("project: procedure arity mismatch");
    }
}

/// @brief Pointer to program defs for function lookup
const std::vector<DefNode> *g_defs = nullptr;

/// @brief Find def by name
/// @ensures returns pointer to DefNode or nullptr
const DefNode *find_def(const std::string &name) {
    // invariant: checked defs[0..i)
    for (const auto &def : *g_defs) {
        if (def.name == name) return &def;
    }
    return nullptr;
}

/// @brief Evaluate leaf or push continuation frames
/// @requires e != nullptr
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void push_eval(const Expr *e, Env &env, std::vector<Frame> &stack,
               std::vector<Value> &values, std::istream &in) {
    switch (e->kind()) {
    case NodeKind::Int:
        values.push_back(expr_cast<IntExpr>(e)->value);
        break;
    case NodeKind::Bool:
        values.push_back(expr_cast<BoolExpr>(e)->value);
        break;
    case NodeKind::Var:
        values.push_back(*env.at(expr_cast<VarExpr>(e)->name));
        break;
    case NodeKind::Read: {
        int64_t val = 0;
        in >> val;
        values.push_back(val);
        break;
    }
    case NodeKind::Unary: {
        auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryFrame{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        auto *bine = expr_cast<BinaryExpr>(e);
        if (bine->op == BinaryOp::And || bine->op == BinaryOp::Or) {
            stack.push_back(IfCondFrame{
                bine->op == BinaryOp::And ? bine->rhs.get() : nullptr,
                bine->op == BinaryOp::Or ? bine->rhs.get() : nullptr});
            stack.push_back(EvalFrame{bine->lhs.get()});
        } else {
            stack.push_back(BinLhsFrame{bine->op, bine->rhs.get()});
            stack.push_back(EvalFrame{bine->lhs.get()});
        }
        break;
    }
    case NodeKind::If: {
        auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfCondFrame{ife->then_branch.get(),
                                     ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
        break;
    }
    case NodeKind::Let: {
        auto *le = expr_cast<LetExpr>(e);
        stack.push_back(LetBindFrame{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
        break;
    }
    case NodeKind::While: {
        auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileCondFrame{we->cond.get(), we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = expr_cast<SetBangExpr>(e);
        stack.push_back(SetBangFrame{se->var_name});
        stack.push_back(EvalFrame{se->expr.get()});
        break;
    }
    case NodeKind::Begin: {
        auto *beg = expr_cast<BeginExpr>(e);
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
        break;
    }
    case NodeKind::Void:
        values.push_back(std::monostate{});
        break;
    case NodeKind::Get:
        values.push_back(*env.at(expr_cast<GetExpr>(e)->name));
        break;
    case NodeKind::Vector: {
        auto *ve = expr_cast<VectorExpr>(e);
        std::vector<const Expr *> remaining;
        for (size_t i = 1; i < ve->elems.size(); ++i) {
            remaining.push_back(ve->elems[i].get());
        }
        stack.push_back(VectorBuildFrame{ve->elems.size(),
                                          std::move(remaining)});
        stack.push_back(EvalFrame{ve->elems[0].get()});
        break;
    }
    case NodeKind::VectorRef: {
        auto *vr = expr_cast<VectorRefExpr>(e);
        stack.push_back(VectorRefVecFrame{vr->index});
        stack.push_back(EvalFrame{vr->vec.get()});
        break;
    }
    case NodeKind::VectorSet: {
        auto *vs = expr_cast<VectorSetExpr>(e);
        stack.push_back(VectorSetVecFrame{vs->index, vs->val.get()});
        stack.push_back(EvalFrame{vs->vec.get()});
        break;
    }
    case NodeKind::VectorLength: {
        auto *vl = expr_cast<VectorLengthExpr>(e);
        stack.push_back(VectorLengthVecFrame{});
        stack.push_back(EvalFrame{vl->vec.get()});
        break;
    }
    case NodeKind::Allocate:
    case NodeKind::Collect:
    case NodeKind::GlobalValue:
    case NodeKind::Closure:
    case NodeKind::AllocateClosure:
        // These are compiler-internal nodes, not interpreted from source
        values.push_back(std::monostate{});
        break;
    case NodeKind::FunRef: {
        auto *fr = expr_cast<FunRefExpr>(e);
        values.push_back(FunctionValue{fr->name, fr->arity});
        break;
    }
    case NodeKind::Apply: {
        auto *ae = expr_cast<ApplyExpr>(e);
        std::vector<const Expr *> remaining;
        for (size_t i = 0; i < ae->args.size(); ++i) {
            remaining.push_back(ae->args[i].get());
        }
        stack.push_back(ApplyFuncFrame{ae->args.size(), std::move(remaining)});
        stack.push_back(EvalFrame{ae->func.get()});
        break;
    }
    case NodeKind::Lambda: {
        auto *la = expr_cast<LambdaExpr>(e);
        auto clos = std::make_shared<ClosureData>();
        for (const auto &p : la->params) {
            clos->params.push_back(p.first);
        }
        clos->body = la->body.get();
        clos->captured = env; // snapshot of enclosing scope (copying closure)
        clos->arity = static_cast<int64_t>(la->params.size());
        values.push_back(ClosureRef{clos});
        break;
    }
    case NodeKind::ProcArity: {
        auto *pa = expr_cast<ProcArityExpr>(e);
        stack.push_back(ProcArityFrame{});
        stack.push_back(EvalFrame{pa->expr.get()});
        break;
    }
    case NodeKind::Inject: {
        auto *ie = expr_cast<InjectExpr>(e);
        stack.push_back(InjectFrame{ie->ftype});
        stack.push_back(EvalFrame{ie->expr.get()});
        break;
    }
    case NodeKind::Project: {
        auto *pe = expr_cast<ProjectExpr>(e);
        stack.push_back(ProjectFrame{pe->ftype});
        stack.push_back(EvalFrame{pe->expr.get()});
        break;
    }
    case NodeKind::TypePredicate: {
        auto *tp = expr_cast<TypePredExpr>(e);
        stack.push_back(TypePredFrame{tp->pred});
        stack.push_back(EvalFrame{tp->expr.get()});
        break;
    }
    case NodeKind::MakeAny: {
        auto *ma = expr_cast<MakeAnyExpr>(e);
        stack.push_back(InjectFrame{nullptr}); // tag taken from the value
        stack.push_back(EvalFrame{ma->expr.get()});
        break;
    }
    case NodeKind::TagOfAny:
        stack.push_back(TagOfAnyFrame{});
        stack.push_back(EvalFrame{expr_cast<TagOfAnyExpr>(e)->expr.get()});
        break;
    case NodeKind::ValueOf:
        stack.push_back(ValueOfFrame{});
        stack.push_back(EvalFrame{expr_cast<ValueOfExpr>(e)->expr.get()});
        break;
    case NodeKind::AnyVectorRef: {
        auto *ar = expr_cast<AnyVectorRefExpr>(e);
        stack.push_back(AnyRefVecFrame{ar->idx.get()});
        stack.push_back(EvalFrame{ar->vec.get()});
        break;
    }
    case NodeKind::AnyVectorSet: {
        auto *as = expr_cast<AnyVectorSetExpr>(e);
        stack.push_back(AnySetVecFrame{as->idx.get(), as->val.get()});
        stack.push_back(EvalFrame{as->vec.get()});
        break;
    }
    case NodeKind::AnyVectorLength:
        stack.push_back(AnyLengthFrame{});
        stack.push_back(
            EvalFrame{expr_cast<AnyVectorLengthExpr>(e)->vec.get()});
        break;
    case NodeKind::Exit:
        throw TrappedError("trapped-error");
    }
}

/// @brief Continuations for the tag operations
/// @ensures returns false if frame is not one of them
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
bool cont_tagging(Frame &frame, std::vector<Value> &values) {
    if (auto *inf = std::get_if<InjectFrame>(&frame)) {
        Value v = values.back(); values.pop_back();
        TypePred tag = inf->ftype ? pred_of_type(inf->ftype)
                                  : tag_of_value(v);
        values.push_back(std::make_shared<TaggedData>(
            TaggedData{std::move(v), tag}));
        return true;
    }
    if (auto *pf = std::get_if<ProjectFrame>(&frame)) {
        Value v = values.back(); values.pop_back();
        const Value &inner = untag(v);
        if (tag_of_value(inner) != pred_of_type(pf->ftype)) {
            throw TrappedError("project: tag mismatch");
        }
        check_shape(inner, pf->ftype);
        values.push_back(inner);
        return true;
    }
    if (auto *tf = std::get_if<TypePredFrame>(&frame)) {
        Value v = values.back(); values.pop_back();
        values.push_back(tag_of_value(untag(v)) == tf->pred);
        return true;
    }
    if (std::get_if<TagOfAnyFrame>(&frame) != nullptr) {
        Value v = values.back(); values.pop_back();
        static const std::map<TypePred, int64_t> codes = {
            {TypePred::Integer, kTagInt}, {TypePred::Boolean, kTagBool},
            {TypePred::Vector, kTagVector},
            {TypePred::Procedure, kTagFunction}, {TypePred::Void, kTagVoid}};
        values.push_back(codes.at(tag_of_value(untag(v))));
        return true;
    }
    if (std::get_if<ValueOfFrame>(&frame) != nullptr) {
        Value v = values.back(); values.pop_back();
        values.push_back(untag(v));
        return true;
    }
    return false;
}

/// @brief Continuations for the any-vector-* operations
/// @ensures returns false if frame is not one of them; traps out of bounds
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
bool cont_any_vector(Frame &frame, std::vector<Frame> &stack,
                     std::vector<Value> &values) {
    if (auto *rv = std::get_if<AnyRefVecFrame>(&frame)) {
        stack.push_back(AnyRefIdxFrame{});
        stack.push_back(EvalFrame{rv->idx});
        return true;
    }
    if (auto *sv = std::get_if<AnySetVecFrame>(&frame)) {
        stack.push_back(AnySetIdxFrame{sv->val});
        stack.push_back(EvalFrame{sv->idx});
        return true;
    }
    if (auto *si = std::get_if<AnySetIdxFrame>(&frame)) {
        stack.push_back(AnySetValFrame{});
        stack.push_back(EvalFrame{si->val});
        return true;
    }
    if (std::get_if<AnyRefIdxFrame>(&frame) == nullptr &&
        std::get_if<AnySetValFrame>(&frame) == nullptr &&
        std::get_if<AnyLengthFrame>(&frame) == nullptr) {
        return false;
    }
    if (std::get_if<AnyLengthFrame>(&frame) != nullptr) {
        Value vec = values.back(); values.pop_back();
        values.push_back(
            static_cast<int64_t>(as_tuple(untag(vec))->elems.size()));
        return true;
    }
    bool is_set = std::get_if<AnySetValFrame>(&frame) != nullptr;
    Value val;
    if (is_set) { val = values.back(); values.pop_back(); }
    Value idx = values.back(); values.pop_back();
    Value vec = values.back(); values.pop_back();
    const Value &inner = untag(vec);
    if (!std::holds_alternative<Tuple>(inner)) {
        throw TrappedError("any-vector: not a tuple");
    }
    auto tup = as_tuple(inner);
    int64_t i = as_int(idx);
    if (i < 0 || i >= static_cast<int64_t>(tup->elems.size())) {
        throw TrappedError("any-vector: index out of bounds");
    }
    if (is_set) {
        tup->elems[static_cast<size_t>(i)] = std::move(val);
        values.push_back(std::monostate{});
    } else {
        values.push_back(tup->elems[static_cast<size_t>(i)]);
    }
    return true;
}

/// @brief Process continuation frame
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void process_cont(Frame &frame, Env &env, std::vector<Frame> &stack,
                  std::vector<Value> &values) {
    if (auto *lf = std::get_if<LetBindFrame>(&frame)) {
        Value val = values.back(); values.pop_back();
        env[lf->var] = std::make_shared<Value>(val); // fresh cell per binding
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
            values.push_back(values_equal(lhs, rhs)); break;
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
            stack.push_back(WhileBodyFrame{wc->cond, wc->body});
            stack.push_back(EvalFrame{wc->body});
        } else {
            values.push_back(std::monostate{}); // while returns void
        }
    } else if (auto *wb = std::get_if<WhileBodyFrame>(&frame)) {
        values.pop_back(); // discard body result
        stack.push_back(WhileCondFrame{wb->cond, wb->body});
        stack.push_back(EvalFrame{wb->cond});
    } else if (auto *sb = std::get_if<SetBangFrame>(&frame)) {
        Value val = values.back(); values.pop_back();
        auto it = env.find(sb->var);
        if (it != env.end()) *it->second = val; // mutate shared cell in place
        else env[sb->var] = std::make_shared<Value>(val);
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
    } else if (auto *vb = std::get_if<VectorBuildFrame>(&frame)) {
        if (vb->remaining.empty()) {
            // Collect all elements from value stack
            auto tup = std::make_shared<TupleData>();
            tup->elems.resize(vb->total);
            for (size_t i = 0; i < vb->total; ++i) {
                tup->elems[vb->total - 1 - i] = values.back();
                values.pop_back();
            }
            values.push_back(Tuple{tup});
        } else {
            const Expr *next = vb->remaining[0];
            std::vector<const Expr *> rest(vb->remaining.begin() + 1,
                                            vb->remaining.end());
            stack.push_back(VectorBuildFrame{vb->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vr = std::get_if<VectorRefVecFrame>(&frame)) {
        Value vec_val = values.back(); values.pop_back();
        auto tup = as_tuple(vec_val);
        values.push_back(tup->elems[static_cast<size_t>(vr->index)]);
    } else if (auto *vs = std::get_if<VectorSetVecFrame>(&frame)) {
        // vec is on stack, now eval val
        stack.push_back(VectorSetValFrame{vs->index});
        stack.push_back(EvalFrame{vs->val});
    } else if (auto *vs = std::get_if<VectorSetValFrame>(&frame)) {
        Value val = values.back(); values.pop_back();
        Value vec_val = values.back(); values.pop_back();
        auto tup = as_tuple(vec_val);
        tup->elems[static_cast<size_t>(vs->index)] = val;
        values.push_back(std::monostate{}); // set returns void
    } else if (std::get_if<VectorLengthVecFrame>(&frame) != nullptr) {
        Value vec_val = values.back(); values.pop_back();
        auto tup = as_tuple(vec_val);
        values.push_back(static_cast<int64_t>(tup->elems.size()));
    } else if (auto *af = std::get_if<ApplyFuncFrame>(&frame)) {
        if (af->remaining.empty()) {
            // func val on stack, no args — dispatch immediately
            stack.push_back(ApplyArgsFrame{af->total_args});
        } else {
            const Expr *next = af->remaining[0];
            std::vector<const Expr *> rest(af->remaining.begin() + 1,
                                            af->remaining.end());
            stack.push_back(ApplyFuncFrame{af->total_args, std::move(rest)});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *aa = std::get_if<ApplyArgsFrame>(&frame)) {
        // Stack: func_val, arg0, ..., argN (top)
        std::vector<Value> args;
        for (size_t i = 0; i < aa->total_args; ++i) {
            args.push_back(values.back());
            values.pop_back();
        }
        std::reverse(args.begin(), args.end());
        Value func_val = values.back(); values.pop_back();
        if (auto *cl = std::get_if<ClosureRef>(&func_val)) {
            // Lexical closure: run body in a fresh env = captured + params,
            // then restore the caller's env.
            const ClosureData &cd = **cl;
            stack.push_back(RestoreEnvFrame{env});
            Env new_env = cd.captured; // shares captured cells with definer
            // invariant: params[0..i) bound as fresh cells in new_env
            for (size_t i = 0; i < cd.params.size(); ++i) {
                new_env[cd.params[i]] = std::make_shared<Value>(args[i]);
            }
            env = std::move(new_env);
            stack.push_back(EvalFrame{cd.body});
        } else if (auto *f = std::get_if<FunctionValue>(&func_val)) {
            const DefNode *def = find_def(f->name);
            if (def == nullptr) {
                throw std::runtime_error("apply: unknown function " + f->name);
            }
            // Top-level functions: bind params in flat env (names unique).
            // invariant: params[0..i) bound as fresh cells
            for (size_t i = 0; i < def->params.size(); ++i) {
                env[def->params[i].first] = std::make_shared<Value>(args[i]);
            }
            stack.push_back(EvalFrame{def->body.get()});
        } else {
            throw std::runtime_error("apply: not a function");
        }
    } else if (std::get_if<ProcArityFrame>(&frame) != nullptr) {
        Value v = values.back(); values.pop_back();
        if (auto *cl = std::get_if<ClosureRef>(&v)) {
            values.push_back((*cl)->arity);
        } else if (auto *f = std::get_if<FunctionValue>(&v)) {
            values.push_back(f->arity);
        } else {
            throw std::runtime_error("procedure_arity: not a function");
        }
    } else if (auto *rf = std::get_if<RestoreEnvFrame>(&frame)) {
        env = std::move(rf->saved);
    } else if (!cont_tagging(frame, values) &&
               !cont_any_vector(frame, stack, values)) {
        throw std::runtime_error("interpret: unexpected continuation frame");
    }
}

} // namespace

/// @brief Interpret program, returning final value
/// @requires prog.body != nullptr
/// @ensures result is the value of prog.body under empty env
Value interpret(const Program &prog, std::istream &in) {
    std::vector<Frame> stack;
    std::vector<Value> values;
    Env env;
    g_defs = &prog.defs;
    // Bind function names in env
    // invariant: env has function values for defs[0..i)
    for (const auto &def : prog.defs) {
        env[def.name] = std::make_shared<Value>(FunctionValue{
            def.name, static_cast<int64_t>(def.params.size())});
    }
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

} // namespace mc
