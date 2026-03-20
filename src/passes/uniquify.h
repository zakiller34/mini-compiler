#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Alpha-rename all variables to unique names (x -> x.1, x.2, ...)
/// @requires prog.body != nullptr
/// @ensures result has all unique variable names, semantics preserved
std::unique_ptr<Program> uniquify(const Program &prog);

} // namespace mc
