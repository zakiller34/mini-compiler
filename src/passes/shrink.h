#pragma once

#include "../ast.h"

#include <memory>

/// @brief Desugar and/or to if expressions
/// @requires prog.body != nullptr, type-checked
/// @ensures no BinaryExpr(And/Or) in output
std::unique_ptr<Program> shrink(const Program &prog);
