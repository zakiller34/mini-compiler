#pragma once

#include "../ir/x86_ir.h"

namespace mc {

/// @brief Add main prelude and conclusion blocks for frame setup/teardown
/// @requires prog has "start" block, stack_space set
/// @ensures result has "main" and "conclusion" blocks added
x86::X86Program generate_prelude_conclusion(const x86::X86Program &prog);

} // namespace mc
