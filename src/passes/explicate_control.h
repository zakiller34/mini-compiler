#pragma once

#include "../ast.h"
#include "../ir/c_ir.h"

/// @brief Convert AST to C_Var IR with explicit control flow
/// @requires prog.body != nullptr, prog is in A-normal form
/// @ensures result has single "start" block with assignments + return
cir::CProgram explicate_control(const Program &prog);
