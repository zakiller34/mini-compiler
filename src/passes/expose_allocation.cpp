#include "expose_allocation.h"

#include "any_rebuild.h"
#include "clone_leaf.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace mc {

namespace {

/// @brief Word size in bytes for heap object layout
constexpr int64_t kWordSize = 8;

/// @brief Generate a fresh temporary name
/// @requires counter >= 0
/// @ensures result is "alloc.N" with N = old counter; counter incremented
std::string fresh_tmp(int &counter) {
    return "alloc." + std::to_string(counter++);
}

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
struct VectorBuild { size_t total; TypePtr vec_type; };
struct VectorRefBuild { int64_t index; };
struct VectorSetVecBuild { int64_t index; const Expr *val; };
struct VectorSetValBuild { int64_t index; };
struct VectorLengthBuild {};
struct ApplyBuild { size_t total; std::vector<const Expr *> remaining; };
struct ClosureBuild { size_t total; int64_t arity; };
struct ProcArityBuild {};

using Frame = std::variant<EvalFrame, UnaryBuild, BinBuildLhs, BinBuildRhs,
                           IfBuildCond, IfBuildThen, IfBuildElse,
                           LetBuildInit, LetBuildBody,
                           WhileBuildCond, WhileBuildBody,
                           SetBangBuild, BeginBuild,
                           VectorBuild, VectorRefBuild,
                           VectorSetVecBuild, VectorSetValBuild,
                           VectorLengthBuild, ApplyBuild,
                           ClosureBuild, ProcArityBuild,
                           AnyBuildFrame>;

/// Types of the variables in scope. Names are unique after uniquify, so a
/// single flat map needs no scoping.
using TypeEnv = std::map<std::string, TypePtr>;

/// @brief Infer the type of one already-lowered tuple element
/// @requires e != nullptr
/// @ensures the result distinguishes pointer-shaped slots (Vector, Any) from
///          scalar ones, which is what the GC pointer mask depends on
// Dispatch over a closed node set: exempt from the 30-line rule (CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
TypePtr infer_elem_type(const Expr *e, const TypeEnv &env) {
    switch (e->kind()) {
    case NodeKind::Int: case NodeKind::Read:
    case NodeKind::TagOfAny: case NodeKind::AnyVectorLength:
    case NodeKind::VectorLength: case NodeKind::ProcArity:
        return int_type();
    case NodeKind::Bool: case NodeKind::TypePredicate:
        return bool_type();
    case NodeKind::Void: case NodeKind::Collect:
        return void_type();
    case NodeKind::Allocate:
        return expr_cast<AllocateExpr>(e)->type;
    case NodeKind::AllocateClosure:
        return expr_cast<AllocateClosureExpr>(e)->type;
    case NodeKind::MakeAny: case NodeKind::Inject:
    case NodeKind::AnyVectorRef:
        return any_type();
    case NodeKind::ValueOf:
        return expr_cast<ValueOfExpr>(e)->ftype;
    case NodeKind::Project:
        return expr_cast<ProjectExpr>(e)->ftype;
    case NodeKind::Var: {
        auto it = env.find(expr_cast<VarExpr>(e)->name);
        return it == env.end() ? nullptr : it->second;
    }
    case NodeKind::Get: {
        auto it = env.find(expr_cast<GetExpr>(e)->name);
        return it == env.end() ? nullptr : it->second;
    }
    default:
        return nullptr;
    }
}

/// @brief Infer the type of a vector literal for Allocate node
/// @requires elems non-empty; elements already expose-allocated
/// @ensures an element whose type cannot be inferred is conservatively Any,
///          which marks it as a possible GC root
TypePtr infer_vector_type(const std::vector<std::unique_ptr<Expr>> &elems,
                          const TypeEnv &env) {
    std::vector<TypePtr> ts;
    // invariant: ts has inferred types for elems[0..i)
    for (const auto &elem : elems) {
        auto t = infer_elem_type(elem.get(), env);
        ts.push_back(t ? t : any_type());
    }
    return vector_type(std::move(ts));
}

/// @brief Conservative closure layout type: an n-slot Vector (a GC root),
///        with each slot marked non-pointer (interior tracing is imprecise).
/// @ensures is_vector_type(result) is true; no slot is itself a Vector
TypePtr closure_layout_type(size_t n) {
    return vector_type(std::vector<TypePtr>(n, int_type()));
}

/// @brief GC check: if free_ptr+bytes < fromspace_end then void else collect.
std::unique_ptr<Expr> make_gc_check(int64_t bytes) {
    return std::make_unique<IfExpr>(
        std::make_unique<BinaryExpr>(
            BinaryOp::Lt,
            std::make_unique<BinaryExpr>(
                BinaryOp::Add,
                std::make_unique<GlobalValueExpr>("free_ptr"),
                std::make_unique<IntExpr>(bytes)),
            std::make_unique<GlobalValueExpr>("fromspace_end")),
        std::make_unique<VoidExpr>(),
        std::make_unique<CollectExpr>(bytes));
}

/// @brief Lower a vector/closure literal: bind elems to temps, GC-check,
///        allocate, initialize slots, return the object.
/// @requires alloc_node is an Allocate/AllocateClosure of length n
std::unique_ptr<Expr> lower_allocation(
    std::vector<std::unique_ptr<Expr>> elem_exprs,
    std::unique_ptr<Expr> alloc_node, int64_t n, int &tmp_counter) {
    std::vector<std::string> tmp_names;
    // invariant: tmp_names has a fresh name per elem[0..i)
    for (size_t i = 0; i < elem_exprs.size(); ++i) {
        tmp_names.push_back(fresh_tmp(tmp_counter));
    }
    std::vector<std::unique_ptr<Expr>> begin_body;
    begin_body.push_back(make_gc_check(kWordSize * (n + 1)));
    std::string v_name = fresh_tmp(tmp_counter);
    // invariant: begin_body has set!s for slots[0..i)
    for (size_t i = 0; i < tmp_names.size(); ++i) {
        begin_body.push_back(std::make_unique<VectorSetExpr>(
            std::make_unique<VarExpr>(v_name), static_cast<int64_t>(i),
            std::make_unique<VarExpr>(tmp_names[i])));
    }
    begin_body.push_back(std::make_unique<VarExpr>(v_name));
    std::unique_ptr<Expr> inner = std::make_unique<LetExpr>(
        v_name, std::move(alloc_node),
        std::make_unique<BeginExpr>(std::move(begin_body)));
    // decreases: i; invariant: inner wrapped with lets for elems[i+1..]
    for (int i = static_cast<int>(tmp_names.size()) - 1; i >= 0; --i) {
        inner = std::make_unique<LetExpr>(
            tmp_names[static_cast<size_t>(i)],
            std::move(elem_exprs[static_cast<size_t>(i)]), std::move(inner));
    }
    return inner;
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void push_eval(const EvalFrame &ef, std::vector<Frame> &stack,
               std::vector<std::unique_ptr<Expr>> &results, TypeEnv &env) {
    const Expr *e = ef.expr;

    if (auto leaf = clone_leaf(e)) {
        results.push_back(std::move(*leaf));
        return;
    }
    if (push_any_eval<EvalFrame>(e, stack)) return;
    switch (e->kind()) {
    case NodeKind::Var:
        results.push_back(std::make_unique<VarExpr>(
            expr_cast<VarExpr>(e)->name));
        break;
    case NodeKind::Get:
        results.push_back(std::make_unique<GetExpr>(
            expr_cast<GetExpr>(e)->name));
        break;
    case NodeKind::Unary: {
        auto *ue = expr_cast<UnaryExpr>(e);
        stack.push_back(UnaryBuild{ue->op});
        stack.push_back(EvalFrame{ue->operand.get()});
        break;
    }
    case NodeKind::Binary: {
        auto *be = expr_cast<BinaryExpr>(e);
        stack.push_back(BinBuildLhs{be->op, be->rhs.get()});
        stack.push_back(EvalFrame{be->lhs.get()});
        break;
    }
    case NodeKind::If: {
        auto *ife = expr_cast<IfExpr>(e);
        stack.push_back(IfBuildCond{ife->then_branch.get(),
                                     ife->else_branch.get()});
        stack.push_back(EvalFrame{ife->cond.get()});
        break;
    }
    case NodeKind::Let: {
        auto *le = expr_cast<LetExpr>(e);
        stack.push_back(LetBuildInit{le->var, le->body.get()});
        stack.push_back(EvalFrame{le->init.get()});
        break;
    }
    case NodeKind::While: {
        auto *we = expr_cast<WhileExpr>(e);
        stack.push_back(WhileBuildCond{we->body.get()});
        stack.push_back(EvalFrame{we->cond.get()});
        break;
    }
    case NodeKind::SetBang: {
        auto *se = expr_cast<SetBangExpr>(e);
        stack.push_back(SetBangBuild{se->var_name});
        stack.push_back(EvalFrame{se->expr.get()});
        break;
    }
    case NodeKind::Begin: {
        auto *beg = expr_cast<BeginExpr>(e);
        if (beg->exprs.empty()) {
            results.push_back(std::make_unique<BeginExpr>(
                std::vector<std::unique_ptr<Expr>>{}));
        } else {
            std::vector<const Expr *> remaining;
            for (size_t i = 1; i < beg->exprs.size(); ++i) {
                remaining.push_back(beg->exprs[i].get());
            }
            stack.push_back(BeginBuild{std::move(remaining),
                                        beg->exprs.size()});
            stack.push_back(EvalFrame{beg->exprs[0].get()});
        }
        break;
    }
    case NodeKind::Vector: {
        auto *ve = expr_cast<VectorExpr>(e);
        // Push all elements for evaluation, then VectorBuild
        // We need a type but don't have it until we know element types
        // Use a placeholder; type is inferred after elements are built
        stack.push_back(VectorBuild{ve->elems.size(), nullptr});
        // Push elements in reverse
        for (int i = static_cast<int>(ve->elems.size()) - 1; i >= 0; --i) {
            stack.push_back(EvalFrame{ve->elems[i].get()});
        }
        break;
    }
    case NodeKind::VectorRef: {
        auto *vr = expr_cast<VectorRefExpr>(e);
        stack.push_back(VectorRefBuild{vr->index});
        stack.push_back(EvalFrame{vr->vec.get()});
        break;
    }
    case NodeKind::VectorSet: {
        auto *vs = expr_cast<VectorSetExpr>(e);
        stack.push_back(VectorSetVecBuild{vs->index, vs->val.get()});
        stack.push_back(EvalFrame{vs->vec.get()});
        break;
    }
    case NodeKind::VectorLength: {
        auto *vl = expr_cast<VectorLengthExpr>(e);
        stack.push_back(VectorLengthBuild{});
        stack.push_back(EvalFrame{vl->vec.get()});
        break;
    }
    case NodeKind::FunRef: {
        auto *fr = expr_cast<FunRefExpr>(e);
        results.push_back(
            std::make_unique<FunRefExpr>(fr->name, fr->arity));
        break;
    }
    case NodeKind::Apply: {
        auto *ap = expr_cast<ApplyExpr>(e);
        size_t total = 1 + ap->args.size();
        std::vector<const Expr *> remaining;
        for (size_t i = 0; i < ap->args.size(); ++i) {
            remaining.push_back(ap->args[i].get());
        }
        stack.push_back(ApplyBuild{total, std::move(remaining)});
        stack.push_back(EvalFrame{ap->func.get()});
        break;
    }
    case NodeKind::Allocate:
        results.push_back(std::make_unique<AllocateExpr>(
            expr_cast<AllocateExpr>(e)->len,
            expr_cast<AllocateExpr>(e)->type));
        break;
    case NodeKind::Collect:
        results.push_back(std::make_unique<CollectExpr>(
            expr_cast<CollectExpr>(e)->bytes));
        break;
    case NodeKind::GlobalValue:
        results.push_back(std::make_unique<GlobalValueExpr>(
            expr_cast<GlobalValueExpr>(e)->name));
        break;
    case NodeKind::Closure: {
        auto *cl = expr_cast<ClosureExpr>(e);
        stack.push_back(ClosureBuild{cl->elems.size(), cl->arity});
        // decreases: i; invariant: elems[i+1..] queued (reverse order)
        for (int i = static_cast<int>(cl->elems.size()) - 1; i >= 0; --i) {
            stack.push_back(EvalFrame{cl->elems[static_cast<size_t>(i)].get()});
        }
        break;
    }
    case NodeKind::ProcArity: {
        auto *pa = expr_cast<ProcArityExpr>(e);
        stack.push_back(ProcArityBuild{});
        stack.push_back(EvalFrame{pa->expr.get()});
        break;
    }
    case NodeKind::AllocateClosure:
        results.push_back(std::make_unique<AllocateClosureExpr>(
            expr_cast<AllocateClosureExpr>(e)->len,
            expr_cast<AllocateClosureExpr>(e)->type,
            expr_cast<AllocateClosureExpr>(e)->arity));
        break;
    }
}

// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void process_cont(Frame &frame, std::vector<Frame> &stack,
                  std::vector<std::unique_ptr<Expr>> &results,
                  int &tmp_counter, TypeEnv &env) {
    if (auto *anyb = std::get_if<AnyBuildFrame>(&frame)) {
        build_any(*anyb, results);
    } else if (auto *ub = std::get_if<UnaryBuild>(&frame)) {
        auto operand = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<UnaryExpr>(
            ub->op, std::move(operand)));
    } else if (auto *bl = std::get_if<BinBuildLhs>(&frame)) {
        stack.push_back(BinBuildRhs{bl->op});
        stack.push_back(EvalFrame{bl->rhs});
    } else if (auto *br = std::get_if<BinBuildRhs>(&frame)) {
        auto rhs = std::move(results.back()); results.pop_back();
        auto lhs = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<BinaryExpr>(
            br->op, std::move(lhs), std::move(rhs)));
    } else if (auto *ic = std::get_if<IfBuildCond>(&frame)) {
        stack.push_back(IfBuildThen{ic->else_br});
        stack.push_back(EvalFrame{ic->then_br});
    } else if (auto *it = std::get_if<IfBuildThen>(&frame)) {
        stack.push_back(IfBuildElse{});
        stack.push_back(EvalFrame{it->else_br});
    } else if (std::get_if<IfBuildElse>(&frame) != nullptr) {
        auto else_e = std::move(results.back()); results.pop_back();
        auto then_e = std::move(results.back()); results.pop_back();
        auto cond_e = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<IfExpr>(
            std::move(cond_e), std::move(then_e), std::move(else_e)));
    } else if (auto *li = std::get_if<LetBuildInit>(&frame)) {
        // The init is already lowered, so its type is known before the body
        if (auto t = infer_elem_type(results.back().get(), env)) {
            env[li->var] = t;
        }
        stack.push_back(LetBuildBody{li->var});
        stack.push_back(EvalFrame{li->body});
    } else if (auto *lb = std::get_if<LetBuildBody>(&frame)) {
        auto body = std::move(results.back()); results.pop_back();
        auto init = std::move(results.back()); results.pop_back();
        if (auto t = infer_elem_type(init.get(), env)) env[lb->var] = t;
        results.push_back(std::make_unique<LetExpr>(
            lb->var, std::move(init), std::move(body)));
    } else if (auto *wc = std::get_if<WhileBuildCond>(&frame)) {
        stack.push_back(WhileBuildBody{});
        stack.push_back(EvalFrame{wc->body});
    } else if (std::get_if<WhileBuildBody>(&frame) != nullptr) {
        auto body = std::move(results.back()); results.pop_back();
        auto cond = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<WhileExpr>(
            std::move(cond), std::move(body)));
    } else if (auto *sb = std::get_if<SetBangBuild>(&frame)) {
        auto expr = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<SetBangExpr>(
            sb->var, std::move(expr)));
    } else if (auto *bb = std::get_if<BeginBuild>(&frame)) {
        if (bb->remaining.empty()) {
            std::vector<std::unique_ptr<Expr>> exprs;
            for (size_t i = 0; i < bb->total; ++i) {
                exprs.push_back(std::move(results.back()));
                results.pop_back();
            }
            std::reverse(exprs.begin(), exprs.end());
            results.push_back(std::make_unique<BeginExpr>(std::move(exprs)));
        } else {
            const Expr *next = bb->remaining[0];
            std::vector<const Expr *> rest(bb->remaining.begin() + 1,
                                            bb->remaining.end());
            stack.push_back(BeginBuild{std::move(rest), bb->total});
            stack.push_back(EvalFrame{next});
        }
    } else if (auto *vb = std::get_if<VectorBuild>(&frame)) {
        // All n elements are on results stack (in order)
        std::vector<std::unique_ptr<Expr>> elem_exprs;
        // invariant: elem_exprs has last i popped; decreases: total-i
        for (size_t i = 0; i < vb->total; ++i) {
            elem_exprs.push_back(std::move(results.back()));
            results.pop_back();
        }
        std::reverse(elem_exprs.begin(), elem_exprs.end());
        TypePtr vtype = infer_vector_type(elem_exprs, env);
        int64_t n = static_cast<int64_t>(vb->total);
        auto alloc = std::make_unique<AllocateExpr>(n, vtype);
        results.push_back(lower_allocation(std::move(elem_exprs),
                                           std::move(alloc), n, tmp_counter));
    } else if (auto *cb = std::get_if<ClosureBuild>(&frame)) {
        std::vector<std::unique_ptr<Expr>> elem_exprs;
        // invariant: elem_exprs has last i popped; decreases: total-i
        for (size_t i = 0; i < cb->total; ++i) {
            elem_exprs.push_back(std::move(results.back()));
            results.pop_back();
        }
        std::reverse(elem_exprs.begin(), elem_exprs.end());
        int64_t n = static_cast<int64_t>(cb->total);
        auto alloc = std::make_unique<AllocateClosureExpr>(
            n, closure_layout_type(cb->total), cb->arity);
        results.push_back(lower_allocation(std::move(elem_exprs),
                                           std::move(alloc), n, tmp_counter));
    } else if (std::get_if<ProcArityBuild>(&frame) != nullptr) {
        auto v = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<ProcArityExpr>(std::move(v)));
    } else if (auto *vr = std::get_if<VectorRefBuild>(&frame)) {
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorRefExpr>(
            std::move(vec), vr->index));
    } else if (auto *vsv = std::get_if<VectorSetVecBuild>(&frame)) {
        stack.push_back(VectorSetValBuild{vsv->index});
        stack.push_back(EvalFrame{vsv->val});
    } else if (auto *vs = std::get_if<VectorSetValBuild>(&frame)) {
        auto val = std::move(results.back()); results.pop_back();
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorSetExpr>(
            std::move(vec), vs->index, std::move(val)));
    } else if (std::get_if<VectorLengthBuild>(&frame) != nullptr) {
        auto vec = std::move(results.back()); results.pop_back();
        results.push_back(std::make_unique<VectorLengthExpr>(std::move(vec)));
    } else if (auto *ab = std::get_if<ApplyBuild>(&frame)) {
        if (ab->remaining.empty()) {
            size_t num_args = ab->total - 1;
            std::vector<std::unique_ptr<Expr>> args;
            for (size_t i = 0; i < num_args; ++i) {
                args.push_back(std::move(results.back()));
                results.pop_back();
            }
            std::reverse(args.begin(), args.end());
            auto func = std::move(results.back()); results.pop_back();
            results.push_back(std::make_unique<ApplyExpr>(
                std::move(func), std::move(args)));
        } else {
            const Expr *next = ab->remaining[0];
            std::vector<const Expr *> rest(ab->remaining.begin() + 1,
                                            ab->remaining.end());
            stack.push_back(ApplyBuild{ab->total, std::move(rest)});
            stack.push_back(EvalFrame{next});
        }
    }
}

/// @brief Process a single expression through expose_allocation
/// @requires root != nullptr
std::unique_ptr<Expr> expose_alloc_expr(const Expr *root, int &tmp_counter,
                                        TypeEnv env) {
    std::vector<Frame> stack;
    std::vector<std::unique_ptr<Expr>> results;
    stack.push_back(EvalFrame{root});

    // decreases: stack.size()
    while (!stack.empty()) {
        auto frame = std::move(stack.back());
        stack.pop_back();
        if (auto *ef = std::get_if<EvalFrame>(&frame)) {
            push_eval(*ef, stack, results, env);
        } else {
            process_cont(frame, stack, results, tmp_counter, env);
        }
    }
    return std::move(results.back());
}

} // namespace

std::unique_ptr<Program> expose_allocation(const Program &prog) {
    int tmp_counter = 0;
    // Seed the environment with the declared types of every function
    TypeEnv globals;
    // invariant: globals has fun types for defs[0..i)
    for (const auto &def : prog.defs) {
        std::vector<TypePtr> params;
        for (const auto &p : def.params) params.push_back(p.second);
        globals[def.name] = fun_type(std::move(params), def.ret_type);
    }

    std::vector<DefNode> new_defs;
    // invariant: new_defs[0..i) processed
    for (const auto &def : prog.defs) {
        TypeEnv env = globals;
        // invariant: env has params[0..j) of this def
        for (const auto &p : def.params) env[p.first] = p.second;
        auto new_body = expose_alloc_expr(def.body.get(), tmp_counter, env);
        new_defs.push_back(DefNode{def.name, def.params, def.ret_type,
                                    std::move(new_body)});
    }
    auto new_body = expose_alloc_expr(prog.body.get(), tmp_counter, globals);
    return std::make_unique<Program>(std::move(new_defs), std::move(new_body));
}

} // namespace mc
