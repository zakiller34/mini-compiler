#pragma once

#include "ast.h"

#include <cstdint>
#include <istream>

/// @brief Interpret program, return final int64 result
/// @requires prog.body != nullptr
/// @ensures result == semantics of prog under input stream in
int64_t interpret(const Program &prog, std::istream &in);
