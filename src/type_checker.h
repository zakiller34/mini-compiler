#pragma once

#include "ast.h"
#include "type.h"

#include <stdexcept>

class TypeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// @brief Type-check program, return type of body
/// @requires prog.body != nullptr
/// @ensures returns type if well-typed, throws TypeError otherwise
TypePtr type_check(const Program &prog);
