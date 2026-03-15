#pragma once

#include "ast.h"

#include <stdexcept>

enum class Type { Int, Bool, Void };

class TypeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// @brief Type-check program, return type of body
/// @requires prog.body != nullptr
/// @ensures returns type if well-typed, throws TypeError otherwise
Type type_check(const Program &prog);
