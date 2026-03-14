#pragma once

#include "../ir/x86_ir.h"

/// @brief Fix illegal two-memory-operand instructions, remove trivial movq
/// @requires prog has x86 with Deref args (no VarArg)
/// @ensures no instruction has two memory operands
x86::X86Program patch_instructions(const x86::X86Program &prog);
