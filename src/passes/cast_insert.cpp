#include "cast_insert.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mc {

namespace {

// -- Builders for the L_Any forms of figure 9.10 --

/// @brief (Any ... -> Any) with `n` parameters
TypePtr any_fun_type(size_t n) {
    std::vector<TypePtr> params(n, any_type());
    return fun_type(std::move(params), any_type());
}

/// @brief (Vector Any ... Any) with `n` elements
TypePtr any_vec_type(size_t n) {
    return vector_type(std::vector<TypePtr>(n, any_type()));
}

std::unique_ptr<Expr> inject(std::unique_ptr<Expr> e, TypePtr t) {
    return std::make_unique<InjectExpr>(std::move(e), std::move(t));
}

std::unique_ptr<Expr> project(std::unique_ptr<Expr> e, TypePtr t) {
    return std::make_unique<ProjectExpr>(std::move(e), std::move(t));
}

/// @brief The tagged constant #f, the only falsy value of L_Dyn
std::unique_ptr<Expr> any_bool(bool v) {
    return inject(std::make_unique<BoolExpr>(v), bool_type());
}

/// @brief (eq? e (inject #f Boolean)) — true exactly when `e` is falsy
std::unique_ptr<Expr> is_falsy(std::unique_ptr<Expr> e) {
    return std::make_unique<BinaryExpr>(BinaryOp::Eq, std::move(e),
                                        any_bool(false));
}

/// @brief Wrap an Int-producing expression, projecting both operands
std::unique_ptr<Expr> int_binop(BinaryOp op, std::unique_ptr<Expr> lhs,
                                std::unique_ptr<Expr> rhs, const TypePtr &res) {
    auto e = std::make_unique<BinaryExpr>(
        op, project(std::move(lhs), int_type()),
        project(std::move(rhs), int_type()));
    return inject(std::move(e), res);
}

// -- Frame machine (no recursion; mirrors the other AST passes) --

struct EvalFrame { const Expr *expr; };
struct UnaryBuild { UnaryOp op; };
struct BinBuildLhs { BinaryOp op; const Expr *rhs; };
struct BinBuildRhs { BinaryOp op; };
struct IfBuildCond { const Expr *then_br; const Expr *else_br; };
struct IfBuildThen { const Expr *else_br; };
struct IfBuildElse {};
struct LetBuildInit { std::string var; const Expr *body; };
struct LetBuildBody { std::string var; };
struct WhileBuildCond { const Expr *body; };
struct WhileBuildBody {};
struct SetBangBuild { std::string var; };
struct BeginBuild { std::vector<const Expr *> remaining; size_t total; };
struct VectorBuild { size_t total; std::vector<const Expr *> remaining; };
struct AnyRefVecBuild { const Expr *idx; };
struct AnyRefIdxBuild {};
struct AnySetVecBuild { const Expr *idx; const Expr *val; };
struct AnySetIdxBuild { const Expr *val; };
struct AnySetValBuild {};
struct LengthBuild {};
struct ApplyBuild { size_t total; std::vector<const Expr *> remaining; };
struct LambdaBuild { std::vector<std::string> params; };
struct ProcArityBuild {};
struct TypePredBuild { TypePred pred; };

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild, VectorBuild,
                           AnyRefVecBuild, AnyRefIdxBuild,
                           AnySetVecBuild, AnySetIdxBuild, AnySetValBuild,
                           LengthBuild, ApplyBuild, LambdaBuild,
                           ProcArityBuild, TypePredBuild>;

using Results = std::vector<std::unique_ptr<Expr>>;

/// @brief Pop the top result
std::unique_ptr<Expr> pop(Results &results) {
    auto r = std::move(results.back());
    results.pop_back();
    return r;
}

/// @brief Handle the leaves, which become tagged constants
/// @requires e != nullptr
/// @ensures returns false if e is not a leaf
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool push_leaf(const Expr *e, Results &results) {
    switch (e->kind()) {
    case NodeKind::Int:
        results.push_back(inject(
            std::make_unique<IntExpr>(expr_cast<IntExpr>(e)->value),
            int_type()));
        return true;
    case NodeKind::Bool:
        results.push_back(any_bool(expr_cast<BoolExpr>(e)->value));
        return true;
    case NodeKind::Read:
        results.push_back(inject(std::make_unique<ReadExpr>(), int_type()));
        return true;
    case NodeKind::Void:
        results.push_back(inject(std::make_unique<VoidExpr>(), void_type()));
        return true;
    case NodeKind::Var:
        results.push_back(
            std::make_unique<VarExpr>(expr_cast<VarExpr>(e)->name));
        return true;
    case NodeKind::Get:
        results.push_back(
            std::make_unique<GetExpr>(expr_cast<GetExpr>(e)->name));
        return true;
    case NodeKind::FunRef: {
        const auto *fr = expr_cast<FunRefExpr>(e);
        results.push_back(inject(
            std::make_unique<FunRefExpr>(fr->name, fr->arity),
            any_fun_type(static_cast<size_t>(fr->arity))));
        return true;
    }
    default:
        return false;
    }
}

/// @brief Push continuation frames for the tuple-shaped nodes
/// @requires e != nullptr
/// @ensures returns false if e is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool push_tuple(const Expr *e, std::vector<Frame> &stack, Results &results) {
    switch (e->kind()) {
    case NodeKind::Vector: {
        const auto *ve = expr_cast<VectorExpr>(e);
        if (ve->elems.empty()) {
            throw std::runtime_error("cast_insert: empty vector");
        }
        std::vector<const Expr *> rest;
        // invariant: rest has elems[1..i)
        for (size_t i = 1; i < ve->elems.size(); ++i) {
            rest.push_back(ve->elems[i].get());
        }
        stack.push_back(VectorBuild{ve->elems.size(), std::move(rest)});
        stack.push_back(EvalFrame{ve->elems[0].get()});
        return true;
    }
    case NodeKind::AnyVectorRef: {
        const auto *ar = expr_cast<AnyVectorRefExpr>(e);
        stack.push_back(AnyRefVecBuild{ar->idx.get()});
        stack.push_back(EvalFrame{ar->vec.get()});
        return true;
    }
    case NodeKind::AnyVectorSet: {
        const auto *as = expr_cast<AnyVectorSetExpr>(e);
        stack.push_back(AnySetVecBuild{as->idx.get(), as->val.get()});
        stack.push_back(EvalFrame{as->vec.get()});
        return true;
    }
    case NodeKind::VectorLength:
        stack.push_back(LengthBuild{});
        stack.push_back(EvalFrame{expr_cast<VectorLengthExpr>(e)->vec.get()});
        return true;
    case NodeKind::AnyVectorLength:
        stack.push_back(LengthBuild{});
        stack.push_back(
            EvalFrame{expr_cast<AnyVectorLengthExpr>(e)->vec.get()});
        return true;
    default:
        (void)results;
        return false;
    }
}

/// @brief Evaluate leaf or push continuation frames (enum-switch FSM)
/// @requires ef.expr != nullptr
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               Results &results) {
    const Expr *e = ef.expr;
    if (push_leaf(e, results) || push_tuple(e, stack, results)) return;

    switch (e->kind()) {
    case NodeKind::Unary: {
        const auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        const auto *be = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{be->op, be->rhs.get()});
        stack.push_back(EvalFrame{be->lhs.get()});
        break;
    }
    case NodeKind::If: {
        const auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                    ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
        break;
    }
    case NodeKind::Let: {
        const auto *le = expr_cast<LetExpr>(e);
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
        break;
    }
    case NodeKind::While: {
        const auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileBuildCond{we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
        break;
    }
    case NodeKind::SetBang: {
        const auto *se = expr_cast<SetBangExpr>(e);
        stack.push_back(SetBangBuild{se->var_name});
        stack.push_back(EvalFrame{se->expr.get()});
        break;
    }
    case NodeKind::Begin: {
        const auto *beg = expr_cast<BeginExpr>(e);
        std::vector<const Expr *> rest;
        // invariant: rest has exprs[1..i)
        for (size_t i = 1; i < beg->exprs.size(); ++i) {
            rest.push_back(beg->exprs[i].get());
        }
        stack.push_back(BeginBuild{std::move(rest), beg->exprs.size()});
        stack.push_back(EvalFrame{beg->exprs[0].get()});
        break;
    }
    case NodeKind::Apply: {
        const auto *ae = expr_cast<ApplyExpr>(e);
        std::vector<const Expr *> rest;
        // invariant: rest has args[0..i)
        for (const auto &a : ae->args) rest.push_back(a.get());
        stack.push_back(ApplyBuild{ae->args.size(), std::move(rest)});
        stack.push_back(EvalFrame{ae->func.get()});
        break;
    }
    case NodeKind::Lambda: {
        const auto *la = expr_cast<LambdaExpr>(e);
        std::vector<std::string> names;
        // invariant: names has params[0..i) names
        for (const auto &p : la->params) names.push_back(p.first);
        stack.push_back(LambdaBuild{std::move(names)});
        stack.push_back(EvalFrame{la->body.get()});
        break;
    }
    case NodeKind::ProcArity:
        stack.push_back(ProcArityBuild{});
        stack.push_back(EvalFrame{expr_cast<ProcArityExpr>(e)->expr.get()});
        break;
    case NodeKind::TypePredicate:
        stack.push_back(TypePredBuild{expr_cast<TypePredExpr>(e)->pred});
        stack.push_back(EvalFrame{expr_cast<TypePredExpr>(e)->expr.get()});
        break;
    default:
        throw std::runtime_error("cast_insert: unexpected node kind");
    }
}

/// @brief Build a unary result: -e projects to Int, `not` tests falsiness
void build_unary(UnaryOp op, Results &results) {
    auto operand = pop(results);
    if (op == UnaryOp::Neg) {
        results.push_back(inject(
            std::make_unique<UnaryExpr>(
                UnaryOp::Neg, project(std::move(operand), int_type())),
            int_type()));
        return;
    }
    // (not e) => (if (eq? e #f) #t #f) — every non-#f value is truthy
    results.push_back(std::make_unique<IfExpr>(
        is_falsy(std::move(operand)), any_bool(true), any_bool(false)));
}

/// @brief Build a binary result once both operands are cast-inserted
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
void build_binary(BinaryOp op, Results &results, int64_t &counter) {
    auto rhs = pop(results);
    auto lhs = pop(results);
    switch (op) {
    case BinaryOp::Add: case BinaryOp::Sub:
        results.push_back(int_binop(op, std::move(lhs), std::move(rhs),
                                    int_type()));
        break;
    case BinaryOp::Lt: case BinaryOp::Le:
    case BinaryOp::Gt: case BinaryOp::Ge:
        results.push_back(int_binop(op, std::move(lhs), std::move(rhs),
                                    bool_type()));
        break;
    case BinaryOp::Eq:
        results.push_back(inject(
            std::make_unique<BinaryExpr>(BinaryOp::Eq, std::move(lhs),
                                          std::move(rhs)),
            bool_type()));
        break;
    case BinaryOp::And:
        // (and a b) => (if (eq? a #f) #f b)
        results.push_back(std::make_unique<IfExpr>(
            is_falsy(std::move(lhs)), any_bool(false), std::move(rhs)));
        break;
    case BinaryOp::Or: {
        // (or a b) => (let t a (if (eq? t #f) b t)) — `a` is used twice
        std::string tmp = "cast." + std::to_string(counter++);
        auto test = is_falsy(std::make_unique<VarExpr>(tmp));
        auto body = std::make_unique<IfExpr>(
            std::move(test), std::move(rhs),
            std::make_unique<VarExpr>(tmp));
        results.push_back(std::make_unique<LetExpr>(
            tmp, std::move(lhs), std::move(body)));
        break;
    }
    }
}

/// @brief Build the tuple-shaped results (vector / any-vector-* / length)
/// @ensures returns false if frame is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool build_tuple(Frame &frame, std::vector<Frame> &stack, Results &results) {
    if (auto *vb = std::get_if<VectorBuild>(&frame)) {
        if (!vb->remaining.empty()) {
            const Expr *next = vb->remaining[0];
            std::vector<const Expr *> rest(vb->remaining.begin() + 1,
                                            vb->remaining.end());
            stack.push_back(VectorBuild{vb->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
            return true;
        }
        Results elems;
        // invariant: elems has the last i results, reversed
        for (size_t i = 0; i < vb->total; ++i) elems.push_back(pop(results));
        std::reverse(elems.begin(), elems.end());
        results.push_back(inject(
            std::make_unique<VectorExpr>(std::move(elems)),
            any_vec_type(vb->total)));
        return true;
    }
    if (auto *rv = std::get_if<AnyRefVecBuild>(&frame)) {
        stack.push_back(AnyRefIdxBuild{});
        stack.push_back(EvalFrame{rv->idx});
        return true;
    }
    if (std::get_if<AnyRefIdxBuild>(&frame) != nullptr) {
        auto idx = pop(results);
        auto vec = pop(results);
        results.push_back(std::make_unique<AnyVectorRefExpr>(
            std::move(vec), project(std::move(idx), int_type())));
        return true;
    }
    if (auto *sv = std::get_if<AnySetVecBuild>(&frame)) {
        stack.push_back(AnySetIdxBuild{sv->val});
        stack.push_back(EvalFrame{sv->idx});
        return true;
    }
    if (auto *si = std::get_if<AnySetIdxBuild>(&frame)) {
        stack.push_back(AnySetValBuild{});
        stack.push_back(EvalFrame{si->val});
        return true;
    }
    if (std::get_if<AnySetValBuild>(&frame) != nullptr) {
        auto val = pop(results);
        auto idx = pop(results);
        auto vec = pop(results);
        results.push_back(inject(
            std::make_unique<AnyVectorSetExpr>(
                std::move(vec), project(std::move(idx), int_type()),
                std::move(val)),
            void_type()));
        return true;
    }
    if (std::get_if<LengthBuild>(&frame) != nullptr) {
        results.push_back(inject(
            std::make_unique<AnyVectorLengthExpr>(pop(results)), int_type()));
        return true;
    }
    return false;
}

/// @brief Build the control-flow results (if / let / while / begin / set!)
/// @ensures returns false if frame is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool build_control(Frame &frame, std::vector<Frame> &stack, Results &results) {
    if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br});
        stack.push_back(EvalFrame{ic->then_br});
        return true;
    }
    if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{});
        stack.push_back(EvalFrame{it->else_br});
        return true;
    }
    if (std::get_if<IfBuildElse>(&frame) != nullptr) {
        // (if c t e) => (if (eq? c #f) e t): the branches swap places
        auto else_br = pop(results);
        auto then_br = pop(results);
        auto cond = pop(results);
        results.push_back(std::make_unique<IfExpr>(
            is_falsy(std::move(cond)), std::move(else_br),
            std::move(then_br)));
        return true;
    }
    if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body});
        return true;
    }
    if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = pop(results);
        auto init = pop(results);
        results.push_back(std::make_unique<LetExpr>(
            lb->var, std::move(init), std::move(body)));
        return true;
    }
    if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{});
        stack.push_back(EvalFrame{wc->body});
        return true;
    }
    if (std::get_if<WhileBuildBody>(&frame) != nullptr) {
        auto body = pop(results);
        auto cond = pop(results);
        auto test = std::make_unique<UnaryExpr>(
            UnaryOp::Not, is_falsy(std::move(cond)));
        results.push_back(inject(
            std::make_unique<WhileExpr>(std::move(test), std::move(body)),
            void_type()));
        return true;
    }
    if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        results.push_back(inject(
            std::make_unique<SetBangExpr>(sb->var, pop(results)),
            void_type()));
        return true;
    }
    if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (!bb->remaining.empty()) {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1,
                                            bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total});
            stack.push_back(EvalFrame{next});
            return true;
        }
        Results exprs;
        // invariant: exprs has the last i results, reversed
        for (size_t i = 0; i < bb->total; ++i) exprs.push_back(pop(results));
        std::reverse(exprs.begin(), exprs.end());
        results.push_back(std::make_unique<BeginExpr>(std::move(exprs)));
        return true;
    }
    return false;
}

/// @brief Build the function-shaped results (apply / lambda / arity)
/// @ensures returns false if frame is not one of them
// Enum-switch FSM / frame dispatcher: exempt from the 30-line rule
// NOLINTNEXTLINE(readability-function-size)
bool build_function(Frame &frame, std::vector<Frame> &stack,
                    Results &results) {
    if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        if (!ab->remaining.empty()) {
            const Expr *next = ab->remaining[0];
            std::vector<const Expr *> rest(ab->remaining.begin() + 1,
                                            ab->remaining.end());
            stack.push_back(ApplyBuild{ab->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
            return true;
        }
        Results args;
        // invariant: args has the last i results, reversed
        for (size_t i = 0; i < ab->total; ++i) args.push_back(pop(results));
        std::reverse(args.begin(), args.end());
        auto func = pop(results);
        results.push_back(std::make_unique<ApplyExpr>(
            project(std::move(func), any_fun_type(ab->total)),
            std::move(args)));
        return true;
    }
    if (auto *lam = std::get_if<LambdaBuild>(&frame)) {
        std::vector<std::pair<std::string, TypePtr>> params;
        // invariant: params has lam->params[0..i) typed Any
        for (const auto &n : lam->params) params.emplace_back(n, any_type());
        size_t arity = params.size();
        results.push_back(inject(
            std::make_unique<LambdaExpr>(std::move(params), any_type(),
                                          pop(results)),
            any_fun_type(arity)));
        return true;
    }
    return false;
}

/// @brief Lower `procedure_arity(e)` where `e` is Any (not in the book's
///        L_Dyn; kept so dynamic programs keep the Phase 7 operator).
/// The arity lives in the closure's heap tag, so any function type works for
/// ValueOf — it only selects the pointer-shaped untagging.
/// @ensures result is Any, or traps when `e` is not a procedure
std::unique_ptr<Expr> build_proc_arity(std::unique_ptr<Expr> operand,
                                       int64_t &counter) {
    std::string tmp = "cast." + std::to_string(counter++);
    auto guard = std::make_unique<BinaryExpr>(
        BinaryOp::Eq,
        std::make_unique<TagOfAnyExpr>(std::make_unique<VarExpr>(tmp)),
        std::make_unique<IntExpr>(kTagFunction));
    auto arity = inject(
        std::make_unique<ProcArityExpr>(std::make_unique<ValueOfExpr>(
            std::make_unique<VarExpr>(tmp), any_fun_type(1))),
        int_type());
    auto body = std::make_unique<IfExpr>(
        std::move(guard), std::move(arity), std::make_unique<ExitExpr>());
    return std::make_unique<LetExpr>(tmp, std::move(operand),
                                      std::move(body));
}

/// @brief Process one continuation frame
void process_cont(Frame &frame, std::vector<Frame> &stack, Results &results,
                  int64_t &counter) {
    if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        build_unary(ub->op, results);
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        build_binary(br->op, results, counter);
    } else if (std::get_if<ProcArityBuild>(&frame) != nullptr) {
        results.push_back(build_proc_arity(pop(results), counter));
    } else if (auto *tp = std::get_if<TypePredBuild>(&frame)) {
        // The predicate itself yields Bool, so the result is injected
        results.push_back(inject(
            std::make_unique<TypePredExpr>(tp->pred, pop(results)),
            bool_type()));
    } else if (!build_control(frame, stack, results) &&
               !build_tuple(frame, stack, results) &&
               !build_function(frame, stack, results)) {
        throw std::runtime_error("cast_insert: unexpected continuation");
    }
}

/// @brief Cast-insert one expression tree
/// @requires root != nullptr
/// @ensures result has type Any
std::unique_ptr<Expr> transform(const Expr *root, int64_t &counter) {
    std::vector<Frame> stack;
    Results results;
    stack.push_back(EvalFrame{root});

    // decreases: stack.size()
    // invariant: results holds the completed subtrees, in evaluation order
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results);
        } else {
            process_cont(frame, stack, results, counter);
        }
    }
    return pop(results);
}

} // namespace

std::unique_ptr<Program> cast_insert(const Program &prog) {
    int64_t counter = 0;
    std::vector<DefNode> new_defs;
    // invariant: new_defs has defs[0..i) with Any signatures
    for (const auto &def : prog.defs) {
        std::vector<std::pair<std::string, TypePtr>> params;
        // invariant: params has def.params[0..j) retyped as Any
        for (const auto &p : def.params) params.emplace_back(p.first,
                                                              any_type());
        new_defs.push_back(DefNode{def.name, std::move(params), any_type(),
                                    transform(def.body.get(), counter)});
    }
    return std::make_unique<Program>(std::move(new_defs),
                                      transform(prog.body.get(), counter));
}

} // namespace mc
