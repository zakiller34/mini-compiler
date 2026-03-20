#pragma once

#include "../ast.h"

#include <memory>
#include <optional>

namespace mc {

/// @brief Clone trivial leaf expression (Int, Bool, Read, Void)
/// @requires e != nullptr
/// @ensures returns cloned leaf or nullopt if not a leaf kind
inline std::optional<std::unique_ptr<Expr>> clone_leaf(const Expr *e) {
    switch (e->kind()) {
    case NodeKind::Int:
        return std::make_unique<IntExpr>(expr_cast<IntExpr>(e)->value);
    case NodeKind::Bool:
        return std::make_unique<BoolExpr>(expr_cast<BoolExpr>(e)->value);
    case NodeKind::Read:
        return std::make_unique<ReadExpr>();
    case NodeKind::Void:
        return std::make_unique<VoidExpr>();
    default:
        return std::nullopt;
    }
}

} // namespace mc
