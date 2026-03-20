#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Replace VarExpr(v) → GetExpr(v) where v is target of any set!
/// @requires prog.body != nullptr (after uniquify)
/// @ensures no VarExpr for any set! target variable
std::unique_ptr<Program> uncover_get(const Program &prog);

} // namespace mc
