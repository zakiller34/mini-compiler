#pragma once

#include "../ast.h"

#include <cassert>
#include <memory>
#include <optional>
#include <vector>

namespace mc {

/// @brief Continuation frame shared by every pass that merely rebuilds the
///        L_Any nodes (Inject/Project/predicates/any-vector-*/make-any/...).
/// The Any nodes bind no variables and name no variables directly, so every
/// AST-rewriting pass treats them the same way: transform the children, then
/// rebuild the node with its payload unchanged.
struct AnyBuildFrame {
    const Expr *node;
    size_t count;
};

/// @brief Direct children of an L_Any node
/// @requires e != nullptr
/// @ensures nullopt iff e is not an L_Any node; children in evaluation order
// Dispatch over a closed node set: exempt from the 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
inline std::optional<std::vector<const Expr *>> any_children(const Expr *e) {
    switch (e->kind()) {
    case NodeKind::Inject:
        return std::vector<const Expr *>{expr_cast<InjectExpr>(e)->expr.get()};
    case NodeKind::Project:
        return std::vector<const Expr *>{expr_cast<ProjectExpr>(e)->expr.get()};
    case NodeKind::TypePredicate:
        return std::vector<const Expr *>{expr_cast<TypePredExpr>(e)->expr.get()};
    case NodeKind::MakeAny:
        return std::vector<const Expr *>{expr_cast<MakeAnyExpr>(e)->expr.get()};
    case NodeKind::TagOfAny:
        return std::vector<const Expr *>{expr_cast<TagOfAnyExpr>(e)->expr.get()};
    case NodeKind::ValueOf:
        return std::vector<const Expr *>{expr_cast<ValueOfExpr>(e)->expr.get()};
    case NodeKind::AnyVectorLength:
        return std::vector<const Expr *>{expr_cast<AnyVectorLengthExpr>(e)->vec.get()};
    case NodeKind::AnyVectorRef: {
        const auto *a = expr_cast<AnyVectorRefExpr>(e);
        return std::vector<const Expr *>{a->vec.get(), a->idx.get()};
    }
    case NodeKind::AnyVectorSet: {
        const auto *a = expr_cast<AnyVectorSetExpr>(e);
        return std::vector<const Expr *>{a->vec.get(), a->idx.get(), a->val.get()};
    }
    case NodeKind::Exit:
        return std::vector<const Expr *>{};
    default:
        return std::nullopt;
    }
}

/// @brief Rebuild an L_Any node from already-transformed children
/// @requires any_children(e) has value and kids.size() == its size
/// @ensures result has the same kind and payload as e
// Dispatch over a closed node set: exempt from the 30-line rule (see CLAUDE.md).
// NOLINTNEXTLINE(readability-function-size)
inline std::unique_ptr<Expr>
rebuild_any(const Expr *e, std::vector<std::unique_ptr<Expr>> kids) {
    switch (e->kind()) {
    case NodeKind::Inject:
        return std::make_unique<InjectExpr>(std::move(kids[0]),
                                            expr_cast<InjectExpr>(e)->ftype);
    case NodeKind::Project:
        return std::make_unique<ProjectExpr>(std::move(kids[0]),
                                             expr_cast<ProjectExpr>(e)->ftype);
    case NodeKind::TypePredicate:
        return std::make_unique<TypePredExpr>(
            expr_cast<TypePredExpr>(e)->pred, std::move(kids[0]));
    case NodeKind::MakeAny:
        return std::make_unique<MakeAnyExpr>(std::move(kids[0]),
                                             expr_cast<MakeAnyExpr>(e)->tag);
    case NodeKind::TagOfAny:
        return std::make_unique<TagOfAnyExpr>(std::move(kids[0]));
    case NodeKind::ValueOf:
        return std::make_unique<ValueOfExpr>(std::move(kids[0]),
                                             expr_cast<ValueOfExpr>(e)->ftype);
    case NodeKind::AnyVectorLength:
        return std::make_unique<AnyVectorLengthExpr>(std::move(kids[0]));
    case NodeKind::AnyVectorRef:
        return std::make_unique<AnyVectorRefExpr>(std::move(kids[0]),
                                                  std::move(kids[1]));
    case NodeKind::AnyVectorSet:
        return std::make_unique<AnyVectorSetExpr>(
            std::move(kids[0]), std::move(kids[1]), std::move(kids[2]));
    case NodeKind::Exit:
        return std::make_unique<ExitExpr>();
    default:
        assert(!"rebuild_any: not an L_Any node");
        return std::make_unique<VoidExpr>();
    }
}

/// @brief Push eval frames for an L_Any node's children plus its build frame
/// @requires make(child) yields this pass's eval frame, carrying any state
///           the pass threads through (rename env, need, ...)
/// @ensures returns false iff e is not an L_Any node
template <typename FrameT, typename MakeEval>
bool push_any_eval_with(const Expr *e, std::vector<FrameT> &stack,
                        MakeEval make) {
    auto kids = any_children(e);
    if (!kids) return false;
    stack.push_back(AnyBuildFrame{e, kids->size()});
    // invariant: children pushed in reverse, so they evaluate left to right
    for (auto it = kids->rbegin(); it != kids->rend(); ++it) {
        stack.push_back(make(*it));
    }
    return true;
}

/// @brief push_any_eval_with for passes whose eval frame is just the pointer
/// @ensures returns false iff e is not an L_Any node
template <typename EvalFrameT, typename FrameT>
bool push_any_eval(const Expr *e, std::vector<FrameT> &stack) {
    return push_any_eval_with(e, stack,
                              [](const Expr *c) { return EvalFrameT{c}; });
}

/// @brief Pop `frame.count` results and rebuild the L_Any node
/// @requires results holds the transformed children on top, in order
inline void build_any(const AnyBuildFrame &frame,
                      std::vector<std::unique_ptr<Expr>> &results) {
    std::vector<std::unique_ptr<Expr>> kids(frame.count);
    // invariant: kids[i..count) filled from the top of results
    for (size_t i = frame.count; i > 0; --i) {
        kids[i - 1] = std::move(results.back());
        results.pop_back();
    }
    results.push_back(rebuild_any(frame.node, std::move(kids)));
}

} // namespace mc
