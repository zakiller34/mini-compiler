#pragma once

#include "../ast.h"
#include "../ir/c_ir.h"

/// @brief Convert AST to C_Var IR with explicit control flow
/// @requires prog.body != nullptr (after shrink + uniquify + uncover_get + RCO)
/// @ensures result.blocks has "start" block; tails are Goto/IfStmt/Return
cir::CProgram explicate_control(const Program &prog);
