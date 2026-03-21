#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Replace VarExpr for known function names with FunRefExpr
/// @requires prog has defs collected
/// @ensures no VarExpr for function names; replaced w/ FunRef
std::unique_ptr<Program> reveal_functions(const Program &prog);

} // namespace mc
