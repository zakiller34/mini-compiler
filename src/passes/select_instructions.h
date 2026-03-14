#pragma once

#include "../ir/c_ir.h"
#include "../ir/x86_ir.h"

/// @brief Select x86 instructions from C_Var IR
/// @requires prog has valid "start" block
/// @ensures result has pseudo-x86 with VarArg (not yet assigned homes)
x86::X86Program select_instructions(const cir::CProgram &prog);
