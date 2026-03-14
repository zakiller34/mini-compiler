#pragma once

#include "../ast.h"

#include <memory>

/// @brief Remove complex operands: flatten to A-normal form
/// @requires prog.body != nullptr, all vars uniquified
/// @ensures all operands of +/- are atoms (IntExpr or VarExpr)
std::unique_ptr<Program> remove_complex_operands(const Program &prog);
