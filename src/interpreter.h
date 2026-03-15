#pragma once

#include "ast.h"

#include <istream>
#include <variant>

using Value = std::variant<int64_t, bool>;

/// @brief Interpret program using explicit stack (no recursion)
/// @requires prog.body != nullptr
/// @ensures result == semantics of prog
Value interpret(const Program &prog, std::istream &in);
