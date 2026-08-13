#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Lower Inject/Project/type-predicates to tag manipulation (9.5)
/// @requires prog is a well-typed L_Any program
/// @ensures no InjectExpr, ProjectExpr, TypePredExpr or ProcArityExpr on an
///          Any operand remains; failed projections and out-of-bounds
///          any-vector accesses become ExitExpr (trapped-error)
std::unique_ptr<Program> reveal_casts(const Program &prog);

} // namespace mc
