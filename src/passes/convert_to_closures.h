#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Closure conversion (Siek Ch.8): lift lambdas to top-level defs,
///        capture free vars into closure tuples, route applications through
///        the closure's code pointer.
/// @requires prog is post-uniquify, post-reveal_functions, post-convert_assign;
///           no ClosureExpr/AllocateClosureExpr present yet
/// @ensures result has no LambdaExpr; every LambdaExpr became a lifted DefNode
///          plus a ClosureExpr at its site; every ApplyExpr calls through
///          closure[0]; every top-level def gains a leading `clos` param
std::unique_ptr<Program> convert_to_closures(const Program &prog);

} // namespace mc
