#pragma once

#include "../ir/x86_ir.h"

namespace mc {

/// @brief Replace VarArg with a register or stack Deref(-N(%rbp))
/// @requires prog has pseudo-x86 with VarArg references
/// @ensures no VarArg remains, stack_space set and 16-aligned
/// @ensures if params is non-null, params[i] is never assigned an argument
///          register that still holds one of params[i+1..]; the entry sequence
///          `movq %rdi,p0; movq %rsi,p1; ...` is a parallel move performed
///          sequentially, so without this it can clobber a source it has not
///          read yet
x86::X86Program assign_homes(const x86::X86Program &prog,
                             const std::vector<std::string> *params = nullptr);

/// @brief Argument registers in System V AMD64 order
const std::vector<x86::Reg> &arg_regs();

} // namespace mc
