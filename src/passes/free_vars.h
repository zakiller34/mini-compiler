#pragma once

#include "../ast.h"

#include <set>
#include <string>
#include <vector>

namespace mc {

/// @brief Free variables of an expression (post-uniquify).
/// @requires e != nullptr; names are globally unique (post-uniquify)
/// @ensures result = { names used via Var/Get/set! } \ { names bound by
///          let / lambda params within e }
std::set<std::string> free_vars(const Expr *e);

/// @brief Free variables of e in deterministic (sorted) order.
/// @requires e != nullptr
/// @ensures result is sort(free_vars(e))
std::vector<std::string> free_vars_sorted(const Expr *e);

/// @brief Append all direct Expr children of e onto work (structural walk).
/// @requires e != nullptr
/// @modifies work
void push_child_exprs(const Expr *e, std::vector<const Expr *> &work);

} // namespace mc
