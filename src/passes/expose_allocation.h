#pragma once

#include "../ast.h"

#include <memory>

namespace mc {

/// @brief Lower vector(e1..en) to allocate+collect+vector-set sequence
/// @requires prog.body != nullptr (after shrink, before RCO)
/// @ensures no VectorExpr in output; replaced by Allocate/Collect/GlobalValue
std::unique_ptr<Program> expose_allocation(const Program &prog);

} // namespace mc
