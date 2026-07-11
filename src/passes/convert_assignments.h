#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Assignment conversion (Siek Ch.8): box every variable that is both
///        assigned (via set!) AND free in some lambda, so mutation is shared
///        through closure capture. Boxed var x: binding becomes a 1-tuple,
///        reads become x[0], `set! x v` becomes x[0]=v.
/// @requires prog is post-uniquify (globally unique names)
/// @ensures no boxed variable is both set! and captured as a bare variable;
///          semantics preserved
std::unique_ptr<Program> convert_assignments(const Program &prog);

} // namespace mc
