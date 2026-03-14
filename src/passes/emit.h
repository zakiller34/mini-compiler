#pragma once

#include "../ir/x86_ir.h"

#include <string>

/// @brief Emit AT&T syntax assembly string from x86 program
/// @requires prog has "main", "start", "conclusion" blocks
/// @ensures result is valid x86-64 AT&T assembly
std::string emit(const x86::X86Program &prog);
