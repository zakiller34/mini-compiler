#pragma once

#include "ast.h"
#include "type.h"

#include <map>
#include <stdexcept>

namespace mc {

class TypeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
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
