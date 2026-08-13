#pragma once

#include "ast.h"
#include "lexer.h"
#include "type.h"

#include <map>
#include <stdexcept>

namespace mc {

class TypeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    TypeError(const std::string &msg, SourceLoc where)
        : std::runtime_error(msg), loc(where) {}

    /// Position of the ill-typed expression. Unknown (line == 0) when the node
    /// was synthesised by a pass rather than parsed — notably in `--dyn` mode,
    /// where type checking runs after cast_insert.
    SourceLoc loc;
};

/// @brief Type-check program, return type of body
/// @requires prog.body != nullptr
/// @ensures returns type if well-typed, throws TypeError otherwise
TypePtr type_check(const Program &prog);

/// @brief Type-check an expression with a given environment
/// @requires expr != nullptr
/// @ensures returns type if well-typed
TypePtr type_check_expr(const Expr *expr,
                        const std::map<std::string, TypePtr> &env);

} // namespace mc
