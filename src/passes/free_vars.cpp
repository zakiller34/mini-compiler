#include "free_vars.h"

#include <algorithm>

namespace mc {

/// @brief Append all direct Expr children of e onto work.
/// @requires e != nullptr
/// @modifies work
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
void push_child_exprs(const Expr *e, std::vector<const Expr *> &work) {
    switch (e->kind()) {
    case NodeKind::Unary:
        work.push_back(expr_cast<UnaryExpr>(e)->operand.get());
        break;
    case NodeKind::Binary: {
        auto *b = expr_cast<BinaryExpr>(e);
        work.push_back(b->lhs.get());
        work.push_back(b->rhs.get());
        break;
    }
    case NodeKind::If: {
        auto *i = expr_cast<IfExpr>(e);
        work.push_back(i->cond.get());
        work.push_back(i->then_branch.get());
        work.push_back(i->else_branch.get());
        break;
    }
    case NodeKind::Let: {
        auto *l = expr_cast<LetExpr>(e);
        work.push_back(l->init.get());
        work.push_back(l->body.get());
        break;
    }
    case NodeKind::While: {
        auto *w = expr_cast<WhileExpr>(e);
        work.push_back(w->cond.get());
        work.push_back(w->body.get());
        break;
    }
    case NodeKind::SetBang:
        work.push_back(expr_cast<SetBangExpr>(e)->expr.get());
        break;
    case NodeKind::Begin: {
        auto *b = expr_cast<BeginExpr>(e);
        // invariant: children[0..i) queued; decreases: exprs.size()-i
        for (const auto &c : b->exprs) work.push_back(c.get());
        break;
    }
    case NodeKind::Vector: {
        auto *v = expr_cast<VectorExpr>(e);
        for (const auto &c : v->elems) work.push_back(c.get());
        break;
    }
    case NodeKind::VectorRef:
        work.push_back(expr_cast<VectorRefExpr>(e)->vec.get());
        break;
    case NodeKind::VectorSet: {
        auto *v = expr_cast<VectorSetExpr>(e);
        work.push_back(v->vec.get());
        work.push_back(v->val.get());
        break;
    }
    case NodeKind::VectorLength:
        work.push_back(expr_cast<VectorLengthExpr>(e)->vec.get());
        break;
    case NodeKind::Apply: {
        auto *a = expr_cast<ApplyExpr>(e);
        work.push_back(a->func.get());
        for (const auto &c : a->args) work.push_back(c.get());
        break;
    }
    case NodeKind::Lambda:
        work.push_back(expr_cast<LambdaExpr>(e)->body.get());
        break;
    case NodeKind::ProcArity:
        work.push_back(expr_cast<ProcArityExpr>(e)->expr.get());
        break;
    case NodeKind::Closure: {
        auto *c = expr_cast<ClosureExpr>(e);
        for (const auto &el : c->elems) work.push_back(el.get());
        break;
    }
    case NodeKind::Inject:
        work.push_back(expr_cast<InjectExpr>(e)->expr.get());
        break;
    case NodeKind::Project:
        work.push_back(expr_cast<ProjectExpr>(e)->expr.get());
        break;
    case NodeKind::TypePredicate:
        work.push_back(expr_cast<TypePredExpr>(e)->expr.get());
        break;
    case NodeKind::MakeAny:
        work.push_back(expr_cast<MakeAnyExpr>(e)->expr.get());
        break;
    case NodeKind::TagOfAny:
        work.push_back(expr_cast<TagOfAnyExpr>(e)->expr.get());
        break;
    case NodeKind::ValueOf:
        work.push_back(expr_cast<ValueOfExpr>(e)->expr.get());
        break;
    case NodeKind::AnyVectorRef: {
        auto *a = expr_cast<AnyVectorRefExpr>(e);
        work.push_back(a->vec.get());
        work.push_back(a->idx.get());
        break;
    }
    case NodeKind::AnyVectorSet: {
        auto *a = expr_cast<AnyVectorSetExpr>(e);
        work.push_back(a->vec.get());
        work.push_back(a->idx.get());
        work.push_back(a->val.get());
        break;
    }
    case NodeKind::AnyVectorLength:
        work.push_back(expr_cast<AnyVectorLengthExpr>(e)->vec.get());
        break;
    default:
        break; // leaves: Int, Bool, Var, Read, Void, Get, Exit, Allocate, ...
    }
}

/// @brief Collect used names and binder names, then difference.
// Dispatch over a closed node/instruction/frame set: exempt from the
// 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
std::set<std::string> free_vars(const Expr *e) {
    std::set<std::string> used;
    std::set<std::string> bound;
    std::vector<const Expr *> work{e};
    // decreases: unvisited subtree nodes; invariant: used/bound cover visited
    while (!work.empty()) {
        const Expr *cur = work.back();
        work.pop_back();
        switch (cur->kind()) {
        case NodeKind::Var:
            used.insert(expr_cast<VarExpr>(cur)->name);
            break;
        case NodeKind::Get:
            used.insert(expr_cast<GetExpr>(cur)->name);
            break;
        case NodeKind::SetBang:
            used.insert(expr_cast<SetBangExpr>(cur)->var_name);
            break;
        case NodeKind::Let:
            bound.insert(expr_cast<LetExpr>(cur)->var);
            break;
        case NodeKind::Lambda:
            // invariant: params[0..i) marked bound
            for (const auto &p : expr_cast<LambdaExpr>(cur)->params) {
                bound.insert(p.first);
            }
            break;
        default:
            break;
        }
        push_child_exprs(cur, work);
    }
    std::set<std::string> result;
    // invariant: result has processed used names not in bound
    for (const auto &name : used) {
        if (bound.find(name) == bound.end()) result.insert(name);
    }
    return result;
}

std::vector<std::string> free_vars_sorted(const Expr *e) {
    auto fv = free_vars(e);
    return {fv.begin(), fv.end()}; // std::set iterates in sorted order
}

} // namespace mc
