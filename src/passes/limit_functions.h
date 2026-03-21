#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Rewrite functions with >6 params to pack excess into tuple
/// @requires valid FunRefs
/// @ensures all defs <=6 params; all ApplyExpr <=6 args
std::unique_ptr<Program> limit_functions(const Program &prog);

} // namespace mc
