#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Compile L_Dyn to L_Any by inserting Inject/Project (Siek 2023, 9.4)
/// @requires prog is an L_Dyn program (no type annotations; every binder Any)
/// @ensures every subexpression of the result has type Any; all defs and
///          lambdas take Any parameters and return Any
std::unique_ptr<Program> cast_insert(const Program &prog);

} // namespace mc
