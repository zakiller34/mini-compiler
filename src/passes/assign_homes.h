#pragma once

#include "../ir/x86_ir.h"

namespace mc {

/// @brief Replace VarArg with stack Deref(-N(%rbp))
/// @requires prog has pseudo-x86 with VarArg references
/// @ensures no VarArg remains, stack_space set and 16-aligned
x86::X86Program assign_homes(const x86::X86Program &prog);

} // namespace mc
